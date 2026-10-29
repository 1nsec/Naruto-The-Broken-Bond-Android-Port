bool Backend::State::OutputEffects(OutputImage& source, BackImage& back, uint32_t x, uint32_t y,
    uint32_t width, uint32_t height, VkBuffer ramp, VkDeviceSize ramp_offset, std::string& evidence) {
  const auto bits = [](float f) { return std::bit_cast<uint32_t>(f); };
  const bool fxaa = present_mode.post_effect != SwapPostEffect::kNone;
  auto& ramped = effects[0];
  if (!OutputTarget(ramped, source.width, source.height, fxaa ? VK_FORMAT_R16G16B16A16_UNORM :
      VK_FORMAT_A2B10G10R10_UNORM_PACK32, false, evidence)) return false;
  if (!OutputPass(source, ramped.image->image, ramped.state, ramped.format, ramped.width, ramped.height,
      fxaa ? "present_gamma_luma_ps" : "present_gamma_ps", 0, {}, ramp, ramp_offset, evidence)) return false;
  OutputImage* input = &ramped;
  if (fxaa) {
    auto& target = effects[1];
    if (!OutputTarget(target, source.width, source.height, VK_FORMAT_A2B10G10R10_UNORM_PACK32, true, evidence)) return false;
    std::array<uint32_t, 12> c{source.width, source.height, bits(1.0f / source.width), bits(1.0f / source.height)};
    if (!OutputPass(*input, target.image->image, target.state, target.format, target.width, target.height,
        present_mode.post_effect == SwapPostEffect::kFxaaExtreme ? "fxaa_extreme_cs" : "fxaa_cs",
        2, c, VK_NULL_HANDLE, 0, evidence)) return false;
    input = &target;
  }
  const auto plan = OutputPlan(present_mode, source.width, source.height, width, height);
  if (plan.size() + 1 > effects.size()) return GraphicsError(evidence, "native-output-plan-too-long");
  Transition(Commands(), back.image, VK_IMAGE_ASPECT_COLOR_BIT, back.state, kCopyDest);
  const VkClearColorValue black{{0, 0, 0, 1}};
  const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdClearColorImage(Commands(), back.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
  for (size_t i = 0; i < plan.size(); ++i) {
    const auto& step = plan[i]; const bool last = i + 1 == plan.size();
    VkImage output = back.image; SyncState* sync = &back.state; VkFormat format = swap_format;
    uint32_t dw = swap_extent.width, dh = swap_extent.height, ox = x, oy = y;
    if (!last) {
      auto& target = effects[2 + i];
      if (!OutputTarget(target, step.width, step.height, VK_FORMAT_A2B10G10R10_UNORM_PACK32, false, evidence)) return false;
      output = target.image->image; sync = &target.state; format = target.format; dw = target.width; dh = target.height;
      ox = oy = 0;
    }
    // The SDK SPIR-V rectangle is already in Vulkan clip coordinates.
    std::array<uint32_t, 12> c{bits(-1.0f + 2.0f * ox / dw), bits(-1.0f + 2.0f * oy / dh),
        bits(2.0f * step.width / dw), bits(2.0f * step.height / dh)};
    auto* p = c.data() + 4;
    const char* shader = "guest_output_bilinear_ps";
    switch (step.pass) {
      case gpu::OutputPass::kBilinear:
        p[0] = ox; p[1] = oy; p[2] = bits(1.0f / step.width); p[3] = bits(1.0f / step.height); break;
      case gpu::OutputPass::kCasSharpen:
        shader = "guest_output_ffx_cas_sharpen_ps";
        p[0] = ox; p[1] = oy; p[2] = bits(-1.0f / (8.0f - 3.0f * present_mode.cas_sharpness)); break;
      case gpu::OutputPass::kCasResample:
        shader = "guest_output_ffx_cas_resample_ps";
        p[0] = ox; p[1] = oy; p[2] = bits(float(input->width) / step.width); p[3] = bits(float(input->height) / step.height);
        p[4] = bits(-1.0f / (8.0f - 3.0f * present_mode.cas_sharpness)); break;
      case gpu::OutputPass::kFsrEasu:
        shader = "guest_output_ffx_fsr_easu_ps";
        p[0] = bits(float(input->width) / step.width); p[1] = bits(float(input->height) / step.height);
        p[2] = bits(1.0f / input->width); p[3] = bits(1.0f / input->height); break;
      case gpu::OutputPass::kFsrRcas:
        shader = "guest_output_ffx_fsr_rcas_ps";
        p[0] = ox; p[1] = oy; p[2] = bits(std::exp2(-present_mode.fsr_sharpness_reduction)); break;
    }
    if (!OutputPass(*input, output, *sync, format, dw, dh, shader, 1, c, VK_NULL_HANDLE, 0, evidence)) return false;
    if (!last) input = &effects[2 + i];
  }
  return true;
}

Backend::State::Acquired Backend::State::AcquireBack(uint32_t& index, std::vector<int32_t>& results,
                                                       std::string& evidence) {
  RECT rect{};
  if (!IsWindow(window) || !GetClientRect(window, &rect) || IsIconic(window) || rect.right <= 0 || rect.bottom <= 0) {
    evidence = "{\"presented\":false,\"skipped\":\"window-empty\"}"; return Acquired::kSkipped;
  }
  const char* reason = nullptr; VkResult result = VK_SUCCESS;
  if (!CloseBatch(reason, result)) { GraphicsError(evidence, reason, result); return Acquired::kFailed; }
#ifdef __ANDROID__
  {
    // After a pause the Activity has a new ANativeWindow (none while it is in
    // the background): the frame is skipped, or the surface follows the window.
    void* current = SDL_GetPointerProperty(SDL_GetWindowProperties(static_cast<SDL_Window*>(window)),
                                           SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
    if (!current) {
      evidence = "{\"presented\":false,\"skipped\":\"no-native-window\"}"; return Acquired::kSkipped;
    }
    if (current != native_window) {
      if (!RetireSwapchain(evidence) || !CreateWindowSurface(evidence)) return Acquired::kFailed;
      recreate_swapchain = true;
    }
  }
#endif
  if (!swapchain || recreate_swapchain || uint32_t(rect.right) != swap_extent.width || uint32_t(rect.bottom) != swap_extent.height)
    if (!CreateSwapchain(rect.right, rect.bottom, evidence)) return Acquired::kFailed;
  // Bound the presentation queue to one pending frame independently of render completion.
  for (auto& back : back_images) if (back.pending) {
    if (!WaitPresent(back.presented, evidence)) return Acquired::kFailed;
    back.pending = false;
  }
  if (!acquire_fence) {
    VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if ((result = vkCreateFence(vulkan->vk, &info, nullptr, &acquire_fence)) != VK_SUCCESS) {
      GraphicsError(evidence, "native-vulkan-acquire-fence", result); return Acquired::kFailed;
    }
  }
  if ((result = vkResetFences(vulkan->vk, 1, &acquire_fence)) != VK_SUCCESS) {
    GraphicsError(evidence, "native-vulkan-acquire-reset", result); return Acquired::kFailed;
  }
  const auto deadline = std::chrono::steady_clock::now() + kWaitBudget;
  do {
    if (!IsWindow(window) || IsIconic(window)) {
      evidence = "{\"presented\":false,\"skipped\":\"window-closed-or-minimized\"}"; return Acquired::kSkipped;
    }
    result = vkAcquireNextImageKHR(vulkan->vk, swapchain, 20'000'000, VK_NULL_HANDLE, acquire_fence, &index);
    results.push_back(int32_t(result));
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
      GetClientRect(window, &rect);
      if (rect.right <= 0 || rect.bottom <= 0) {
        evidence = "{\"presented\":false,\"skipped\":\"window-empty\"}"; return Acquired::kSkipped;
      }
      if (!CreateSwapchain(rect.right, rect.bottom, evidence)) return Acquired::kFailed;
    } else if (result != VK_TIMEOUT && result != VK_NOT_READY) break;
  } while (std::chrono::steady_clock::now() < deadline && !failures->Failed());
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    Observe(result); GraphicsError(evidence, "native-vulkan-acquire", result); return Acquired::kFailed;
  }
#ifndef __ANDROID__
  // Android reports suboptimal for the identity transform on a rotated panel
  // (CreateSwapchain), every frame: it is not a reason to recreate there.
  if (result == VK_SUBOPTIMAL_KHR) recreate_swapchain = true;
#endif
  acquire_pending = true;
  if (!WaitPresent(acquire_fence, evidence)) return Acquired::kFailed;
  acquire_pending = false;
  if (!OpenBatch(reason, result)) { GraphicsError(evidence, reason, result); return Acquired::kFailed; }
  return Acquired::kImage;
}

bool Backend::State::QueuePresent(uint32_t index, uint64_t& sequence, VkResult& present_result,
                                  std::string& evidence) {
  auto& back = back_images.at(index);
  const Use present{VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0};
  Transition(Commands(), back.image, VK_IMAGE_ASPECT_COLOR_BIT, back.state, present);
  sequence = batch.sequence;
  submit_signal = back.rendered;
  const char* reason = nullptr; VkResult result = VK_SUCCESS;
  if (!CloseBatch(reason, result)) return GraphicsError(evidence, reason, result);
  if (present_fences && (result = vkResetFences(vulkan->vk, 1, &back.presented)) != VK_SUCCESS)
    return GraphicsError(evidence, "native-present-fence-reset", result);
  VkSwapchainPresentFenceInfoEXT retirement{VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT};
  retirement.swapchainCount = 1; retirement.pFences = &back.presented;
  VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, present_fences ? &retirement : nullptr};
  info.waitSemaphoreCount = 1; info.pWaitSemaphores = &back.rendered;
  info.swapchainCount = 1; info.pSwapchains = &swapchain; info.pImageIndices = &index;
  auto* vulkan_queue = static_cast<plume::VulkanCommandQueue*>(queue.get())->queue;
  const bool authorized = failures->Authorize([&] {
    std::lock_guard lock(*vulkan_queue->mutex);
    result = vkQueuePresentKHR(vulkan_queue->vk, &info);
  });
  if (!authorized) return GraphicsError(evidence, "native-present-refused-after-failure");
  back.pending = present_fences;
  ++presents;
  present_result = result;
#ifdef __ANDROID__
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR) recreate_swapchain = true;
  else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
#else
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) recreate_swapchain = true;
  else if (result != VK_SUCCESS) {
#endif
    Observe(result); return GraphicsError(evidence, "native-vulkan-present", result);
  }
  return true;
}

// The game image of a stage slot into the back image: the gamma ramp, the
// output effects and the scaling to the window (black bars around).
bool Backend::State::DrawStage(OutputImage& stage, BackImage& back, std::string& evidence) {
  VkResult result = VK_SUCCESS;
  State::UploadPage* page{}; VkDeviceSize ramp_offset{};
  if (!Upload(4096, page, ramp_offset, result)) return GraphicsError(evidence, "native-present-ramp-upload", result);
  auto* ramp = reinterpret_cast<uint32_t*>(page->buffer->mapped + ramp_offset);
  const bool brightness = std::fabs(present_mode.brightness - 0.5f) > 0.001f;
  const double exponent = 1.0 / std::clamp(1.0 + (double(present_mode.brightness) - 0.5) * 1.2, 0.1, 4.0);
  for (uint32_t i = 0; i < 256; ++i) for (uint32_t channel = 0; channel < 4; ++channel) {
    const uint32_t value = channel == 3 ? 0 : gamma[channel * 256 + i];
    ramp[4 * i + channel] = brightness ? uint32_t(std::lround(1023.0 * std::pow(std::min(value, 1023u) / 1023.0, exponent))) : value;
  }
  const double aspect_scale = std::min(double(swap_extent.width) / stage.width, double(swap_extent.height) / stage.height);
  const uint32_t vw = std::max(1u, uint32_t(std::lround(stage.width * aspect_scale)));
  const uint32_t vh = std::max(1u, uint32_t(std::lround(stage.height * aspect_scale)));
  const uint32_t x = (swap_extent.width - vw) / 2, y = (swap_extent.height - vh) / 2;
  const auto bits = [](float f) { return std::bit_cast<uint32_t>(f); };
  if (present_mode.effect != PresentEffect::kBilinear || present_mode.post_effect != SwapPostEffect::kNone) {
    if (!OutputEffects(stage, back, x, y, vw, vh, page->buffer->buffer, ramp_offset, evidence)) return false;
  } else {
    const std::array<uint32_t, 12> c{bits(float(x)), bits(float(y)), bits(float(stage.width) / vw), bits(float(stage.height) / vh),
        stage.width, stage.height, bits(float(vw)), bits(float(vh))};
    if (!OutputPass(stage, back.image, back.state, swap_format, swap_extent.width, swap_extent.height,
                      "present_ps", 0, c, page->buffer->buffer, ramp_offset, evidence)) return false;
  }
  return true;
}

bool Backend::Present(uint32_t slot, bool capture, std::vector<uint8_t>& bgra, uint32_t& width,
                       uint32_t& height, std::string& evidence) {
  auto& s = *state_; bgra.clear(); width = height = 0;
  if (!s.ready || s.owner_thread != GetCurrentThreadId() || !s.window || slot >= s.stages.size())
    return GraphicsError(evidence, "native-present-owner-or-slot");
  auto& stage = s.stages[slot];
  if (!stage.staged || stage.presented) return GraphicsError(evidence, "native-present-slot-not-staged");
  uint32_t index = 0;
  std::vector<int32_t> acquire_results;
  switch (s.AcquireBack(index, acquire_results, evidence)) {
    case State::Acquired::kSkipped: stage.presented = true; return true;
    case State::Acquired::kFailed: return false;
    case State::Acquired::kImage: break;
  }
  auto& back = s.back_images.at(index);
  VkResult result = VK_SUCCESS;
  if (!s.DrawStage(stage, back, evidence)) return false;
  // The PC overlay (options menu, title screen, FPS counter) over the image.
  if (!s.DrawOverlay(back, evidence)) return false;
  std::shared_ptr<VmaBuffer> readback;
  const VkDeviceSize bytes = VkDeviceSize(s.swap_extent.width) * s.swap_extent.height * 4;
  if (capture) {
    result = CreateBuffer(s.vulkan->allocator, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT, readback);
    if (result != VK_SUCCESS) return GraphicsError(evidence, "native-present-readback", result);
    s.batch.retained.push_back(readback);
    Transition(s.Commands(), back.image, VK_IMAGE_ASPECT_COLOR_BIT, back.state, kCopySource);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {s.swap_extent.width, s.swap_extent.height, 1};
    vkCmdCopyImageToBuffer(s.Commands(), back.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback->buffer, 1, &copy);
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(s.Commands(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
  }
  stage.last_use = s.batch.sequence;
  uint64_t sequence = 0; VkResult present_result = VK_SUCCESS;
  if (!s.QueuePresent(index, sequence, present_result, evidence)) return false;
  stage.presented = true;
  s.last_presented_slot = int(slot);
  s.MaybeSavePipelineCache();
  s.LogDrawCensus();
  if (capture) {
    if (!s.completion->WaitFor(sequence, kWaitBudget)) return GraphicsError(evidence, "native-present-capture-wait", VK_TIMEOUT);
    if ((result = vmaInvalidateAllocation(s.vulkan->allocator, readback->allocation, 0, bytes)) != VK_SUCCESS)
      return GraphicsError(evidence, "native-present-capture-invalidate", result);
    width = s.swap_extent.width; height = s.swap_extent.height;
    bgra.assign(readback->mapped, readback->mapped + bytes);
  }
  evidence = fmt::format("{{\"presented\":true,\"backend\":\"vulkan\",\"present_result\":{},\"acquire_results\":[{}],"
      "\"generation\":{},\"image\":{},\"size\":[{},{}],\"fence\":{},\"present_mode\":{},\"effective_frame_latency\":1,"
      "\"overlay\":\"{}\"}}",
      int32_t(present_result), fmt::join(acquire_results, ","), s.swap_generation, index, s.swap_extent.width,
      s.swap_extent.height, sequence, int(s.swap_mode), JsonText(s.overlay_status));
  return true;
}

// A frame under the PC overlay while the game presents nothing (the PC title
// screen holds the boot; long loads; a slow frame of a phone with the FPS
// counter on): the game's last image again, so the screen holds still instead
// of flashing black between the game's frames; black before the first. The
// stage slots are only read.
bool Backend::PresentIdle(std::string& evidence) {
  auto& s = *state_;
  if (!s.ready || s.owner_thread != GetCurrentThreadId() || s.operation_poisoned || s.unproved_execution)
    return GraphicsError(evidence, "native-idle-owner-or-state");
  if (!s.window) return GraphicsError(evidence, "native-idle-no-window");
  uint32_t index = 0;
  std::vector<int32_t> acquire_results;
  switch (s.AcquireBack(index, acquire_results, evidence)) {
    case State::Acquired::kSkipped: return true;
    case State::Acquired::kFailed: return false;
    case State::Acquired::kImage: break;
  }
  auto& back = s.back_images.at(index);
  const bool last_image = s.last_presented_slot >= 0 && size_t(s.last_presented_slot) < s.stages.size() &&
                          s.stages[s.last_presented_slot].image && s.stages[s.last_presented_slot].presented;
  if (last_image) {
    if (!s.DrawStage(s.stages[s.last_presented_slot], back, evidence)) return false;
  } else {
    s.batch.list->setFramebuffer(nullptr);
    const auto commands = s.Commands();
    Transition(commands, back.image, VK_IMAGE_ASPECT_COLOR_BIT, back.state, kCopyDest, true);
    const VkClearColorValue black{{0, 0, 0, 1}};
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(commands, back.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
  }
  if (!s.DrawOverlay(back, evidence)) return false;
  ++s.batch.operations;
  uint64_t sequence = 0; VkResult present_result = VK_SUCCESS;
  if (!s.QueuePresent(index, sequence, present_result, evidence)) return false;
  evidence = fmt::format("{{\"presented\":true,\"idle\":true,\"backend\":\"vulkan\",\"present_result\":{},"
      "\"image\":{},\"fence\":{},\"last_image\":{},\"overlay\":\"{}\"}}", int32_t(present_result), index,
      sequence, last_image, JsonText(s.overlay_status));
  return true;
}
