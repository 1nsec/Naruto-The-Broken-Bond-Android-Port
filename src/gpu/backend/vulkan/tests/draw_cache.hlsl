// The draw layout of the Vulkan backend: the VS, PS and shared constants are
// the dynamic uniform buffers at bindings 1 to 3 of set 3.
[[vk::binding(1, 3)]] cbuffer VertexConstants { float4 vs_offset : packoffset(c0); };
[[vk::binding(2, 3)]] cbuffer PixelConstants { float4 ps_color : packoffset(c0); };
[[vk::binding(3, 3)]] cbuffer SharedConstants {
  uint image : packoffset(c0.x);
  uint sampler_index : packoffset(c12.x);
};
[[vk::binding(0, 0)]] Texture2D<float4> textures[];
[[vk::binding(0, 3)]] SamplerState samplers[];
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex vs_main(uint id : SV_VertexID) {
  Vertex v;
  v.uv = float2((id << 1) & 2, id & 2);
  v.position = float4(v.uv * float2(2, -2) + float2(-1, 1) + vs_offset.xy, 0, 1);
  return v;
}
float4 ps_main(Vertex v) : SV_Target {
  return textures[image].SampleLevel(samplers[sampler_index], v.uv, 0) * ps_color;
}
