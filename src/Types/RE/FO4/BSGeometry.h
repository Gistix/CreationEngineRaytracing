#pragma once

namespace RE
{
	enum class BSGeometryType : std::uint8_t
	{
		kGeometry                      = 0,   // Base BSGeometry
		kParticles                     = 1,   // NiParticles, NiParticleSystem, NiParticleMeshes, NiMeshParticleSystem
		kStripParticles                = 2,   // BSStripParticleSystem
		kTriShape                      = 3,   // BSTriShape, BSMergeInstancedTriShape
		kDynamicTriShape               = 4,   // BSDynamicTriShape
		kMeshLODTriShape               = 5,   // BSMeshLODTriShape
		kLODMultiIndexTriShape         = 6,   // BSLODMultiIndexTriShape
		kMultiIndexTriShape            = 7,   // BSMultiIndexTriShape
		kSubIndexTriShape              = 8,   // BSSubIndexTriShape
		kSubIndexLandTriShape          = 9,   // Terrain / Land LOD SubIndex
		kMultiStreamInstanceTriShape   = 10,  // BSMultiStreamInstanceTriShape
		kParticleShaderDynamicTriShape = 11,  // Dynamic particle trishape buffer
		kLines                         = 12,  // BSLines
		kDynamicLines                  = 13,  // BSDynamicLines
		kInstanceGroup                 = 14,  // BSInstanceGroup / NVFlex::DebrisInstanceGroup
		kCombinedTriShape              = 15   // BSCombinedTriShape
	};
}
