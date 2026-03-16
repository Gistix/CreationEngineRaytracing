#pragma once

class TopLevelAS;

struct ITLASUpdateListener
{
	virtual ~ITLASUpdateListener() = default;
	virtual void OnTLASResized(TopLevelAS& tlas) = 0;
};