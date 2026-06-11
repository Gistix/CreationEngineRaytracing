#pragma once

struct PointLightRuntimeData
{
	float constAttenuation;
	float linearAttenuation;
	float quadraticAttenuation;
	float spotOuterAngle;
	float spotInnerAngle;
};
