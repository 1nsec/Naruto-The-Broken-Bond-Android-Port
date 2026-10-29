// Included after Backend::State, inside gpu::vulkan.
// The PC overlay (gpu/overlay.h: PC Options, the PC title screen, the FPS
// counter, toasts) over the back image, last before its presentation, as the
// D3D12 backend's OverlayRenderer draws it: the latest frame the UI thread
// recorded, alpha blended, its textures uploaded once and kept while a frame
// refers to them. The program is backend/overlay_pass.hlsl (overlay_vs,
// overlay_ps): the texture and the linear and point samplers in set 0, the
// constants as push constants of both stages.

void Backend::State::OverlayFailed(const char* reason, VkResult result) {
  overlay_failed = true;
  overlay_status = fmt::format("failed: {} ({})", reason, int32_t(result));
#ifdef __ANDROID__
  __android_log_print(ANDROID_LOG_ERROR, "narutobb", "PC overlay not drawn: %s", overlay_status.c_str());
#endif
}

bool Backend::State::EnsureOverlay(VkFormat format, std::shared_ptr<GraphicsPipeline>& pipeline, const char*& reason,
                                   VkResult& result) {
  result = VK_SUCCESS;
  if (!overlay_layout) {
    const VkDescriptorSetLayoutBinding bindings[]{
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set.bindingCount = uint32_t(std::size(bindings)); set.pBindings = bindings;
    if (!overlay_set && (result = vkCreateDescriptorSetLayout(vulkan->vk, &set, nullptr, &overlay_set)) != VK_SUCCESS) {
      reason = "native-overlay-set-layout"; return false;
    }
    const VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1; layout.pSetLayouts = &overlay_set;
    layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
    if ((result = vkCreatePipelineLayout(vulkan->vk, &layout, nullptr, &overlay_layout)) != VK_SUCCESS) {
      reason = "native-overlay-layout"; return false;
    }
  }
  if (!overlay_samplers) {
    auto samplers = std::make_shared<GraphicsResources>(); samplers->device = vulkan->vk;
    for (const VkFilter filter : {VK_FILTER_LINEAR, VK_FILTER_NEAREST}) {
      VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
      info.minFilter = info.magFilter = filter;
      info.mipmapMode = filter == VK_FILTER_LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
      info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      info.maxLod = VK_LOD_CLAMP_NONE;
      VkSampler sampler{};
      if ((result = vkCreateSampler(vulkan->vk, &info, nullptr, &sampler)) != VK_SUCCESS) {
        reason = "native-overlay-sampler"; return false;
      }
      samplers->samplers.push_back(sampler);
    }
    overlay_samplers = std::move(samplers);
  }
  auto& made = overlay_pipelines[format];
  if (made) { pipeline = made; return true; }
  const auto vs = PassShader("overlay_vs", ShaderStage::kVertex), ps = PassShader("overlay_ps", ShaderStage::kPixel);
  if (!vs.Present() || !ps.Present()) {
    // A shader cache built before the overlay pass existed.
    result = VK_ERROR_INITIALIZATION_FAILED; reason = "native-overlay-shaders-missing"; return false;
  }
  auto object = std::make_shared<GraphicsPipeline>(); object->device = vulkan->vk;
  if ((result = MakeRenderPass(vulkan->vk, format, false, 1, object->pass)) != VK_SUCCESS) {
    reason = "native-overlay-render-pass"; return false;
  }
  VkShaderModule modules[2]{};
  VkPipelineShaderStageCreateInfo stages[2]{};
  for (int i = 0; i < 2; ++i) {
    const auto& bytes = *(i ? ps.bytes : vs.bytes);
    if (bytes.size() % 4) { result = VK_ERROR_INVALID_SHADER_NV; break; }
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = bytes.size(); info.pCode = reinterpret_cast<const uint32_t*>(bytes.data());
    if ((result = vkCreateShaderModule(vulkan->vk, &info, nullptr, &modules[i])) != VK_SUCCESS) break;
    stages[i] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
    stages[i].module = modules[i]; stages[i].pName = "main";
  }
  if (result == VK_SUCCESS) {
    // overlay::Vertex: position, uv, color (R in the low byte).
    const VkVertexInputBindingDescription binding{0, sizeof(overlay::Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[]{
        {0, 0, VK_FORMAT_R32G32_SFLOAT, uint32_t(offsetof(overlay::Vertex, x))},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, uint32_t(offsetof(overlay::Vertex, u))},
        {2, 0, VK_FORMAT_R8G8B8A8_UNORM, uint32_t(offsetof(overlay::Vertex, color))}};
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount = 1; input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = uint32_t(std::size(attributes)); input.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE; raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.blendEnable = VK_TRUE;
    attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.colorBlendOp = VK_BLEND_OP_ADD;
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1; blend.pAttachments = &attachment;
    const VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = uint32_t(std::size(states)); dynamic.pDynamicStates = states;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2; info.pStages = stages; info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly; info.pViewportState = &viewport; info.pRasterizationState = &raster;
    info.pMultisampleState = &ms; info.pDepthStencilState = &ds; info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic; info.layout = overlay_layout; info.renderPass = object->pass;
    result = vkCreateGraphicsPipelines(vulkan->vk, pipeline_cache, 1, &info, nullptr, &object->pipeline);
  }
  for (auto module : modules) if (module) vkDestroyShaderModule(vulkan->vk, module, nullptr);
  if (result != VK_SUCCESS) { reason = "native-overlay-pipeline"; return false; }
  made = pipeline = std::move(object);
  return true;
}

bool Backend::State::PrepareOverlayTexture(const overlay::Texture& texture, OverlayTexture& gpu, const char*& reason,
                                           VkResult& result) {
  result = VK_SUCCESS;
  const VkDeviceSize bytes = VkDeviceSize(texture.width) * texture.height * 4;
  if ((result = CreateImage(vulkan->allocator, VK_FORMAT_R8G8B8A8_UNORM, texture.width, texture.height, 1, false,
                            gpu.image)) != VK_SUCCESS) {
    reason = "native-overlay-texture"; return false;
  }
  UploadPage* page = nullptr; VkDeviceSize offset = 0;
  if (!Upload(bytes, page, offset, result)) { reason = "native-overlay-texture-upload"; return false; }
  std::memcpy(page->buffer->mapped + offset, texture.rgba.data(), size_t(bytes));
  const VkBuffer source = page->buffer->buffer;
  batch.list->setFramebuffer(nullptr);
  const auto commands = Commands();
  Transition(commands, gpu.image->image, VK_IMAGE_ASPECT_COLOR_BIT, gpu.state, kCopyDest, true);
  VkBufferImageCopy copy{};
  copy.bufferOffset = offset;
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.imageExtent = {texture.width, texture.height, 1};
  vkCmdCopyBufferToImage(commands, source, gpu.image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
  Transition(commands, gpu.image->image, VK_IMAGE_ASPECT_COLOR_BIT, gpu.state, kShaderRead);
  gpu.objects = std::make_shared<GraphicsResources>(); gpu.objects->device = vulkan->vk;
  VkImageView view{};
  if ((result = gpu.objects->View(gpu.image->image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1, false,
                                  kIdentityMapping, view)) != VK_SUCCESS) {
    reason = "native-overlay-texture-view"; return false;
  }
  const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1}, {VK_DESCRIPTOR_TYPE_SAMPLER, 2}};
  VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool.maxSets = 1; pool.poolSizeCount = uint32_t(std::size(sizes)); pool.pPoolSizes = sizes;
  if ((result = vkCreateDescriptorPool(vulkan->vk, &pool, nullptr, &gpu.objects->pool)) != VK_SUCCESS) {
    reason = "native-overlay-pool"; return false;
  }
  VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  alloc.descriptorPool = gpu.objects->pool; alloc.descriptorSetCount = 1; alloc.pSetLayouts = &overlay_set;
  if ((result = vkAllocateDescriptorSets(vulkan->vk, &alloc, &gpu.set)) != VK_SUCCESS) {
    reason = "native-overlay-set"; return false;
  }
  const VkDescriptorImageInfo image{VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  const VkDescriptorImageInfo linear{overlay_samplers->samplers[0], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
  const VkDescriptorImageInfo point{overlay_samplers->samplers[1], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
  VkWriteDescriptorSet writes[3]{};
  const VkDescriptorImageInfo* infos[]{&image, &linear, &point};
  for (uint32_t i = 0; i < 3; ++i) {
    writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[i].dstSet = gpu.set; writes[i].dstBinding = i; writes[i].descriptorCount = 1;
    writes[i].descriptorType = i ? VK_DESCRIPTOR_TYPE_SAMPLER : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[i].pImageInfo = infos[i];
  }
  vkUpdateDescriptorSets(vulkan->vk, 3, writes, 0, nullptr);
  return true;
}

bool Backend::State::DrawOverlay(BackImage& back, std::string& evidence) {
  // Textures no frame refers to anymore, released once the work that may read them completed.
  for (auto it = overlay_textures.begin(); it != overlay_textures.end();) {
    if (it->second.source.expired()) {
      batch.retained.push_back(it->second.image);
      batch.retained.push_back(it->second.objects);
      it = overlay_textures.erase(it);
    } else {
      ++it;
    }
  }
  const auto frame = overlay::Latest();
  if (overlay_failed || !frame || frame->commands.empty() || frame->indices.empty() || frame->vertices.empty() ||
      frame->width <= 0 || frame->height <= 0)
    return true;
  const char* reason = nullptr; VkResult result = VK_SUCCESS;
  std::shared_ptr<GraphicsPipeline> pipeline;
  if (!EnsureOverlay(swap_format, pipeline, reason, result)) {
    OverlayFailed(reason, result);
    return true;  // No overlay; the game goes on.
  }
  if (!overlay_white) {
    auto white = std::make_shared<overlay::Texture>();
    white->width = white->height = 1; white->rgba = {255, 255, 255, 255};
    overlay_white = std::move(white);
  }
  // New textures first: their copies run before the render pass.
  const auto texture_of = [&](const std::shared_ptr<const overlay::Texture>& texture) -> OverlayTexture* {
    const auto& source = texture ? texture : overlay_white;
    if (auto it = overlay_textures.find(source.get()); it != overlay_textures.end()) return &it->second;
    if (!source->width || !source->height || source->rgba.size() < size_t(source->width) * source->height * 4)
      return nullptr;
    OverlayTexture gpu;
    gpu.source = source;
    if (!PrepareOverlayTexture(*source, gpu, reason, result)) {
      // The batch may have recorded its copy already: what it refers to stays until it completed.
      if (gpu.image) batch.retained.push_back(gpu.image);
      if (gpu.objects) batch.retained.push_back(gpu.objects);
      return nullptr;
    }
    return &overlay_textures.emplace(source.get(), std::move(gpu)).first->second;
  };
  std::vector<const OverlayTexture*> bound(frame->commands.size());
  for (size_t i = 0; i < frame->commands.size(); ++i) {
    bound[i] = texture_of(frame->commands[i].texture);
    if (reason) {
      Observe(result);
      OverlayFailed(reason, result);
      return true;  // No overlay; the game goes on.
    }
  }
  const VkDeviceSize vertex_bytes = frame->vertices.size() * sizeof(overlay::Vertex);
  const VkDeviceSize index_bytes = frame->indices.size() * sizeof(uint32_t);
  UploadPage* page = nullptr; VkDeviceSize vertex_offset = 0, index_offset = 0;
  if (!Upload(vertex_bytes, page, vertex_offset, result)) return GraphicsError(evidence, "native-overlay-vertex-upload", result);
  std::memcpy(page->buffer->mapped + vertex_offset, frame->vertices.data(), size_t(vertex_bytes));
  const VkBuffer vertices = page->buffer->buffer;
  if (!Upload(index_bytes, page, index_offset, result)) return GraphicsError(evidence, "native-overlay-index-upload", result);
  std::memcpy(page->buffer->mapped + index_offset, frame->indices.data(), size_t(index_bytes));
  const VkBuffer indices = page->buffer->buffer;

  const uint32_t width = swap_extent.width, height = swap_extent.height;
  auto objects = std::make_shared<GraphicsResources>(); objects->device = vulkan->vk;
  batch.retained.push_back(objects); batch.retained.push_back(pipeline);
  VkImageView target{};
  if ((result = objects->View(back.image, swap_format, VK_IMAGE_ASPECT_COLOR_BIT, 1, false, kIdentityMapping, target)) != VK_SUCCESS)
    return GraphicsError(evidence, "native-overlay-view", result);
  if ((result = objects->Framebuffer(pipeline->pass, {target}, width, height)) != VK_SUCCESS)
    return GraphicsError(evidence, "native-overlay-framebuffer", result);
  batch.list->setFramebuffer(nullptr);
  const auto commands = Commands();
  Transition(commands, back.image, VK_IMAGE_ASPECT_COLOR_BIT, back.state, kColorTarget);
  VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  begin.renderPass = pipeline->pass; begin.framebuffer = objects->framebuffer; begin.renderArea.extent = {width, height};
  vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
  const VkViewport viewport{0, 0, float(width), float(height), 0, 1};
  vkCmdSetViewport(commands, 0, 1, &viewport);
  vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
  vkCmdBindVertexBuffers(commands, 0, 1, &vertices, &vertex_offset);
  vkCmdBindIndexBuffer(commands, indices, index_offset, VK_INDEX_TYPE_UINT32);
  const float sx = float(width) / frame->width, sy = float(height) / frame->height;
  struct Constants {
    float scale[2];
    uint32_t textured, point_filter;
  } constants{{2.0f / frame->width, 2.0f / frame->height}, 0, 0};
  VkDescriptorSet current{};
  uint32_t draws = 0;
  for (size_t i = 0; i < frame->commands.size(); ++i) {
    const auto& command = frame->commands[i];
    if (!bound[i] || !command.index_count || size_t(command.first_index) + command.index_count > frame->indices.size())
      continue;
    int32_t left = 0, top = 0, right = int32_t(width), bottom = int32_t(height);
    if (command.scissor) {
      left = std::clamp(int32_t(std::floor(command.left * sx)), 0, int32_t(width));
      top = std::clamp(int32_t(std::floor(command.top * sy)), 0, int32_t(height));
      right = std::clamp(int32_t(std::ceil(command.right * sx)), 0, int32_t(width));
      bottom = std::clamp(int32_t(std::ceil(command.bottom * sy)), 0, int32_t(height));
    }
    if (right <= left || bottom <= top) continue;
    const VkRect2D scissor{{left, top}, {uint32_t(right - left), uint32_t(bottom - top)}};
    vkCmdSetScissor(commands, 0, 1, &scissor);
    if (bound[i]->set != current) {
      current = bound[i]->set;
      vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, overlay_layout, 0, 1, &current, 0, nullptr);
    }
    constants.textured = command.texture ? 1 : 0;
    constants.point_filter = command.texture && !command.texture->linear ? 1 : 0;
    vkCmdPushConstants(commands, overlay_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(constants), &constants);
    vkCmdDrawIndexed(commands, command.index_count, 1, command.first_index, 0, 0);
    ++draws;
  }
  vkCmdEndRenderPass(commands);
  ++batch.operations;
  if (!overlay_frames++) {
    overlay_status = fmt::format("drawn ({} textures)", overlay_textures.size());
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "narutobb", "PC overlay: first frame drawn (%ux%u, %u draws)", width,
                        height, draws);
#endif
  } else {
    overlay_status = "drawn";
  }
  return true;
}

void Backend::State::ReleaseOverlay() {
  overlay_textures.clear();
  overlay_pipelines.clear();
  overlay_samplers.reset();
  overlay_white.reset();
  if (overlay_layout) vkDestroyPipelineLayout(vulkan->vk, overlay_layout, nullptr);
  if (overlay_set) vkDestroyDescriptorSetLayout(vulkan->vk, overlay_set, nullptr);
  overlay_layout = VK_NULL_HANDLE; overlay_set = VK_NULL_HANDLE;
  overlay_failed = false; overlay_frames = 0; overlay_status = "none";
}
