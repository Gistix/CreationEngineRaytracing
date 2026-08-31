#ifndef WAVE_SIZE_HLSLI
#define WAVE_SIZE_HLSLI

#if defined(__hlsl_dx_compiler) && (__SHADER_TARGET_MAJOR > 6 || (__SHADER_TARGET_MAJOR == 6 && __SHADER_TARGET_MINOR >= 6))
#   define WAVE_SIZE(x) [WaveSize(x)]
#else
#   define WAVE_SIZE(x)
#endif

#endif // WAVE_SIZE_HLSLI
