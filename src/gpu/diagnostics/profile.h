#pragma once

// Host timers of the own frame. Each slot
// sums nanoseconds and calls over a frame on whatever thread runs the measured
// work (render thread, engine owner or another guest thread); the frame layer
// takes and resets them at each own Swap of the measurement mode
// (REX_NATIVE_MEASURE_SWAPS). steady_clock is QueryPerformanceCounter here.
#include <array>
#include <atomic>
#include <chrono>
#include "gpu/diagnostics/diagnostics.h"
#include <cstdint>
#include <cstdlib>

namespace gpu::profile {
enum Slot : unsigned {
  kDispatchRoundTrip,    // Dispatcher::Run, caller side: queue, owner and result.
  kEngineOperation,      // The operation itself, on the engine owner.
  kEngineDraw,           // Engine::DrawIndexed.
  kEnginePipeline,       // CreateGraphicsPipelineState inside DrawIndexed, or the take (and wait) of a precompiled one.
  kEngineTexture,        // Sampled texture creation inside DrawIndexed, and UploadSampled.
  kEngineBuffer,         // UploadBuffer (vertex and index buffers kept by the engine).
  kEngineUpload,         // Upload buffer creation, map and copies inside DrawIndexed.
  kEngineRecord,         // Allocator, command list and recording inside DrawIndexed.
  kEngineSubmit,         // ExecuteCommandLists and Signal inside DrawIndexed.
  kEngineClear,          // Engine::ClearSurfaces.
  kEngineResolve,        // Engine::ResolveSurface.
  kEngineWaitIdle,       // Engine::WaitIdle.
  kDraw,                 // OwnDraw, entry to return.
  kDrawOriginal,         // RunOverDiscard of the draw commit.
  kDrawShaders,          // Bound shader read, fetch patch and identification.
  kDrawTextures,         // BindTexture of the PS samplers (a read and upload when an event changed one).
  kDrawTextureHash,      // The content hash of the texture verification only (TextureVerify).
  kDrawShaderLoad,       // LoadDxil/LoadShader (the spec constant variant from the package included).
  kDrawStreams,          // Index and vertex reads, endian swap and expansion.
  kDrawEngine,           // Dispatcher::Run of the draw (round trip).
  kDrawEvidence,         // NoteStartDraw and draw records.
  kSwap,                 // Own Swap hook.
  kSwapWaitIdle,         // CloseFrameBatch: the WaitIdle round trip.
  kClear,                // Own clear hooks (ClearF, BeginTiling, Resolve clears).
  kResolve,              // Own resolve hooks.
  kFenceWait,            // BlockOnFence hook.
  kSwapPace,             // The limiter at the end of the own Swap.
  // The own work of the hooks outside the draw (time of a frame that no
  // slot above measured), and the creation of a sampled copy in the engine.
  kHookCreate,           // D210 CreateTexture and D330 CreateSurface, after the original.
  kHookDestroy,          // D750: the release of the own resources of a header.
  kHookLock,             // D950 Lock (with the original), and the C7E8 texture lock after its original.
  kHookUnlock,           // DBE0, the last Unlock: write ranges and the texture upload (read, distill, post).
  kHookShader,           // 82179200/82179380: the registration of a shader object.
  kEngineTextureCreate,  // CreateCommittedResource of a sampled copy inside UploadSampled.
  kTranslate,            // TranslateDraw, on the translator when deferred, inside kDraw otherwise.
  // The GPU's execution of the submitted batches (Vulkan timestamps), the
  // title thread's own time between its Swaps (its logic and its draws, without
  // the Swap's waits), and the parts of kDrawStreams: the index buffer and the
  // vertex streams (BindBuffer).
  kGpu,
  kGameFrame,
  kStreamsIndex,
  kStreamsVertex,
  kCount
};
inline constexpr const char* kSlotNames[kCount] = {
    "dispatch_round_trip", "engine_operation", "engine_draw", "engine_pipeline", "engine_texture", "engine_buffer",
    "engine_upload", "engine_record", "engine_submit", "engine_clear", "engine_resolve", "engine_wait_idle",
    "draw", "draw_original", "draw_shaders", "draw_textures", "draw_texture_hash", "draw_shader_load",
    "draw_streams", "draw_engine", "draw_evidence", "swap", "swap_wait_idle", "clear", "resolve", "fence_wait",
    "swap_pace", "hook_create", "hook_destroy", "hook_lock", "hook_unlock", "hook_shader", "engine_texture_create",
    "translate", "gpu", "game_frame", "streams_index", "streams_vertex"};
enum Counter : unsigned {
  kHashedBytes, kUploadBytes, kTextureUploads, kPipelinesCreated, kShaderVariants, kSubmissions,
  // Buffer uploads of the engine, their bytes, and the stream and index bytes still carried by draws.
  kBufferUploads, kBufferUploadBytes, kBufferInlineBytes,
  // Draw batches posted to the owner, and the constant bytes the draws carried (whole blocks and deltas).
  kDrawBatches, kConstantBytes,
  // Texture entries registered, bases forgotten
  // by the D750 and bases registered again after a D750, over the frame; and,
  // sampled at the Swap that closes it, the Swap number, the texture bases the
  // engine keeps a copy of, the live headers of the resource registry and the
  // hosted buffers.
  kTexturesRegistered, kTexturesForgotten, kTexturesRecreated, kSwapNumber, kTextureBases, kHeadersLive,
  kBuffersHosted,
  // Pipelines a draw took from the precompiled set (ready, or
  // waited for while a pool thread created it), and pool creations.
  kPipelinesAdopted, kPipelinesWaited, kPipelinesPrecompiled,
  // Write-watch notifications of the SDK and their pages, and the draws
  // with user clip planes (PA_CL_CLIP_CNTL.ucp_ena).
  kWatchNotifications, kWatchPages, kClipPlaneDraws,
  // Pipelines posted to the asynchronous threads by draws, and the draws
  // recorded without themselves while their pipeline was being created.
  kPipelinesAsync, kPipelineSkippedDraws,
  // The translation stage, per frame: the enqueues that waited
  // for room and the deepest the queue got (sampled at the Swap).
  kStageWaits, kStageDepth,
  // Absent pipelines of batched draws created in place because their triple
  // was warm, and those slower than the warm limit (which turns it off in the
  // skip take; a driver that does not keep the shaders otherwise).
  kPipelinesWarm, kPipelinesWarmSlow,
  // Pipelines the owner posted before drawing their batch, and the longest
  // wait of a draw for a pipeline being created in the frame (Max, microseconds).
  kPipelinesPrefetched, kPipelineWaitUsMax,
  // The draw layer's render passes (Vulkan), and the uploads recorded in a
  // batch's prelude instead of ending one; batches submitted where a draw
  // started a render pass (kBatchOperationsEarly).
  kRenderPasses, kPreludeUploads, kEarlySubmits,
  // Draws kept in the open render pass with its other attachment (a draw
  // without depth test on its color target, a depth-only draw on its depth
  // target); why draw passes ended: another target, a barrier, another command
  // (clear, resolve, copy), the batch's end; clears, and those that ended a pass.
  kPassMergedDraws, kPassEndTarget, kPassEndBarrier, kPassEndCommand, kPassEndBatch, kClears, kClearsInPass,
  // Draws of the water reflection left out (water_reflections off), of the
  // characters' shadows (character_shadows off) and beyond the draw distance
  // (draw_distance).
  kReflectionDrawsSkipped, kShadowDrawsSkipped, kDistanceDrawsSkipped,
  kCounterCount
};
inline constexpr const char* kCounterNames[kCounterCount] = {
    "hashed_bytes", "upload_bytes", "texture_uploads", "pipelines_created", "shader_variants", "submissions",
    "buffer_uploads", "buffer_upload_bytes", "buffer_inline_bytes", "draw_batches", "constant_bytes",
    "textures_registered", "textures_forgotten", "textures_recreated", "swap_number", "texture_bases",
    "headers_live", "buffers_hosted", "pipelines_adopted", "pipelines_waited", "pipelines_precompiled",
    "watch_notifications", "watch_pages", "clip_plane_draws", "pipelines_async", "pipeline_skipped_draws",
    "stage_waits", "stage_depth", "pipelines_warm", "pipelines_warm_slow", "pipelines_prefetched",
    "pipeline_wait_us_max", "render_passes", "prelude_uploads", "early_submits", "pass_merged_draws",
    "pass_end_target", "pass_end_barrier", "pass_end_command", "pass_end_batch", "clears", "clears_in_pass",
    "reflection_draws_skipped", "shadow_draws_skipped", "distance_draws_skipped"};

// One cache line each: the title thread, the translator and the engine owner
// add to their own slots at every draw, which must not share a line.
struct alignas(64) Accumulator {
  std::atomic<uint64_t> ns{0}, calls{0};
};
inline std::array<Accumulator, kCount> slots;
inline std::array<std::atomic<uint64_t>, kCounterCount> counters{};

// The measurement mode (REX_NATIVE_MEASURE_SWAPS set) keeps the production
// journal: a record written for every frame is kept only for its first
// kQuietRecords calls.
constexpr uint64_t kQuietRecords = 16;
inline bool QuietRecords() {
  static const bool value = [] {
    const char* text = std::getenv("REX_NATIVE_MEASURE_SWAPS");
    return text && *text;
  }();
  return value;
}

#if defined(__ANDROID__) && defined(__aarch64__)
// The generic timer's virtual counter, which the kernel lets user space read
// (the vDSO's clock): a profile timer runs around each draw on several
// threads, and clock_gettime took a large share of their time on a
// Snapdragon 865. No barrier before the read (a timer of a few microseconds
// needs no ordering, and the barrier drained the pipeline at every timer) and
// a fixed-point scale instead of two divisions.
inline uint64_t Now() {
  static const uint64_t scale = [] {
    uint64_t hz;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(hz));
    if (!hz) hz = 19200000;
    return uint64_t((static_cast<unsigned __int128>(1000000000ull) << 32) / hz);  // ns per tick, 32.32
  }();
  uint64_t ticks;
  asm volatile("mrs %0, cntvct_el0" : "=r"(ticks));
  return uint64_t((static_cast<unsigned __int128>(ticks) * scale) >> 32);
}
#else
inline uint64_t Now() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
#endif
inline void Add(Slot slot, uint64_t ns, uint64_t calls = 1) {
  slots[slot].ns.fetch_add(ns, std::memory_order_relaxed);
  slots[slot].calls.fetch_add(calls, std::memory_order_relaxed);
}
// The profile runs with the diagnostic instruments (gpu::Diagnostics) and in
// the measurement mode, which does not need them, and for the performance log
// (perf_log.h); off, a timer or a count costs one test.
inline bool Profiling() {
  static const bool value = gpu::Diagnostics() || QuietRecords() || std::getenv("REX_NATIVE_PERF_LOG");
  return value;
}
inline void Count(Counter counter, uint64_t value) {
  if (Profiling()) counters[counter].fetch_add(value, std::memory_order_relaxed);
}
// A counter that keeps the frame's largest value instead of the sum.
inline void Max(Counter counter, uint64_t value) {
  if (!Profiling()) return;
  uint64_t seen = counters[counter].load(std::memory_order_relaxed);
  while (seen < value && !counters[counter].compare_exchange_weak(seen, value, std::memory_order_relaxed)) {}
}

// The slots timed around every draw, thousands of times per frame, whose
// calls cost about the same each time: one call in kSampleEvery is timed on
// each thread and counts kSampleEvery times (the per-second sums stay right on
// average; the timers cost an eighth). The slots that take long only now and
// then (a pipeline creation, a texture read, a shader load) time every call.
inline constexpr uint64_t kSampleEvery = 8;
constexpr bool Sampled(Slot slot) {
  return slot == kEngineDraw || slot == kEngineUpload || slot == kEngineRecord || slot == kDraw ||
         slot == kDrawOriginal || slot == kDrawShaders || slot == kDrawStreams || slot == kDrawEngine ||
         slot == kDrawEvidence || slot == kTranslate || slot == kStreamsIndex || slot == kStreamsVertex;
}

class Timer {
 public:
  explicit Timer(Slot slot) : slot_(Take(slot)), start_(slot_ == kCount ? 0 : Now()) {}
  ~Timer() { Stop(); }
  void Stop() {
    if (slot_ == kCount) return;
    const uint64_t ns = Now() - start_;
    if (Sampled(slot_)) Add(slot_, ns * kSampleEvery, kSampleEvery);
    else Add(slot_, ns);
    slot_ = kCount;
  }
  Timer(const Timer&) = delete;
  Timer& operator=(const Timer&) = delete;
 private:
  static Slot Take(Slot slot) {
    if (slot == kCount || !Profiling()) return kCount;
    if (!Sampled(slot)) return slot;
    thread_local std::array<uint8_t, kCount> calls{};
    return calls[slot]++ % kSampleEvery == 0 ? slot : kCount;
  }
  Slot slot_;
  uint64_t start_;
};
}  // namespace gpu::profile
