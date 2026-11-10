// The PC overlay (gpu/overlay.h: PC Options, the PC title screen, the FPS
// counter, toasts) in the Vulkan backend: Dear ImGui's triangles, alpha blended
// over the back buffer at the end of a present (vulkan/overlay.inc). The D3D12
// backend compiles the same program from its own copy at run time
// (d3d12/overlay_renderer.cpp); this file is only built as SPIR-V
// (tools/native-renderer/build-pass-spirv.py): the texture and its two samplers
// in set 0, the D3D12 root constants as push constants for both stages.
//
// A vertex is the position in the frame's coordinate space (pixels from the
// top left), its texture coordinate and its color (R8G8B8A8_UNORM, Dear
// ImGui's order). -fvk-invert-y turns the D3D clip space of vs_main into
// Vulkan's, so the top of the frame stays at the top of the window.
[[vk::binding(0, 0)]] Texture2D<float4> image : register(t0);
[[vk::binding(1, 0)]] SamplerState linear_sampler : register(s0);
[[vk::binding(2, 0)]] SamplerState point_sampler : register(s1);

struct OverlayConstants {
  float2 scale;       // 2 / the frame's size.
  uint textured;      // 0: the vertex color alone.
  uint point_filter;  // The texture is sampled without filtering (pixel art).
};
[[vk::push_constant]] OverlayConstants constants;

struct VsIn {
  [[vk::location(0)]] float2 position : POSITION;
  [[vk::location(1)]] float2 uv : TEXCOORD;
  [[vk::location(2)]] float4 color : COLOR;
};
struct PsIn {
  float4 position : SV_Position;
  float2 uv : TEXCOORD;
  float4 color : COLOR;
};

PsIn vs_main(VsIn i) {
  PsIn o;
  o.position = float4(i.position.x * constants.scale.x - 1.0, 1.0 - i.position.y * constants.scale.y, 0.0, 1.0);
  o.uv = i.uv;
  o.color = i.color;
  return o;
}

float4 ps_main(PsIn i) : SV_Target {
  float4 color = i.color;
  if (constants.textured != 0)
    color *= constants.point_filter != 0 ? image.Sample(point_sampler, i.uv) : image.Sample(linear_sampler, i.uv);
  return color;
}
