#pragma once

#include <omm-gpu-nvrhi.h>

class Renderer;
struct Model;
struct Mesh;

class OMMBaker
{
    inline static std::mutex m_OMMBakerMutex;

    eastl::unique_ptr<omm::GpuBakeNvrhi> m_Baker;

    bool RecordBuild(Mesh* mesh, nvrhi::ICommandList* commandList);
    bool FinalizeBuild(Mesh* mesh);
public:
    OMMBaker();
    void Bake(Model* model);
};