#ifndef VULKAN_HLSLI
#define VULKAN_HLSLI

// ---------------------------------------------------------------------------
// D3D register bindings vs. Vulkan/SPIR-V bindings
// ---------------------------------------------------------------------------
//
// In D3D a resource is placed with a register annotation such as:
//
//     Texture2D<float4> Textures[] : register(t0, space4);
//     RWByteAddressBuffer Output   : register(u0);
//     ConstantBuffer<Foo> FooCB    : register(b3);
//     SamplerState Sampler         : register(s0);
//
// The register type (t/u/b/s) selects a descriptor *category* (SRV, UAV, CBV,
// sampler), the number is the slot inside that category, and `space` is an
// independent register space. Categories never overlap each other.
//
// Vulkan has no register types or spaces. Every resource is identified by a
// single flat pair: (descriptor set, binding). So `register(t0, space4)` maps
// to binding 0 in set 4, `register(u0)` to binding 0 in set 0, etc. Register
// type is ignored, so a t0 and a u0 in the same space would collide.
//
// By default DXC derives (set, binding) from `: register(...)`, applying the
// `-fvk-{t,s,b,u}-shift` command-line shifts. An explicit
// `[[vk::binding(binding, set)]]` attribute overrides that derivation (it takes
// precedence over the register and the shifts).
//
// This project's Vulkan backend uses its own descriptor set layout, which is
// not always identical to the D3D register spaces (bindless tables are
// compacted/reordered), and some UAVs would otherwise collide with SRVs in the
// same set. We therefore keep the D3D `register(...)` for the D3D path and add
// an explicit Vulkan binding only when compiling SPIR-V.
//
// Push constants are Vulkan-only: a dedicated block, not a descriptor. There
// is no D3D register equivalent; on D3D the same struct is just a normal
// constant buffer at `register(bN)`. At most one push constant block may be
// statically used per entry point.
//
// Usage:
//
//     VK_PUSH_CONSTANT ConstantBuffer<Foo> FooCB : register(b3);
//     VK_BINDING(4, 0) Texture2D<float4> Textures[] : register(t0, space4);
//
// On SPIR-V the macros expand to the `[[vk::...]]` attributes; on D3D they
// expand to nothing and the `register(...)` annotation is used as usual.
// ---------------------------------------------------------------------------

#if defined(__spirv__)
// __spirv__ is defined implicitly by DXC when compiling with -spirv.
#   define VK_PUSH_CONSTANT [[vk::push_constant]]
// VK_BINDING(set, index) -> binding `index` in descriptor set `set`.
#   define VK_BINDING(set, index) [[vk::binding(index, set)]]
#else
#   define VK_PUSH_CONSTANT
#   define VK_BINDING(set, index)
#endif

#endif // VULKAN_HLSLI
