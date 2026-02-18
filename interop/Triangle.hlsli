#ifndef TRIANGLE_HLSL
#define TRIANGLE_HLSL

#include "Interop.h"

struct Triangle
{
	uint16_t x;
	uint16_t y;
	uint16_t z;
};
VALIDATE_SIZE(Triangle, 6);

#endif