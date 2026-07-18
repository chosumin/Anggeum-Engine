#pragma once
#include "Status.h"

namespace Core
{
    class Timer;
    class Window;
    class Scene;
    class RenderContext;
    class TransferContext;
    class WorkerThreadManager;
    class IRenderPipeline;

    struct EngineOptions
    {
        bool BenchmarkEnabled{ false };
        Core::Window* window{ nullptr };
    };

    class Engine
    {
    public:
        Engine(const EngineOptions& options);
        ~Engine();

        void Update();
        void Draw();
        void WaitIdle();
    private:
        unique_ptr<Core::Status> _status;
        unique_ptr<Core::Timer> _timer;
        Device* _device;
        IRenderPipeline* _renderPipeline;
        RenderContext* _renderContext;
        Scene* _scene;
        TransferContext* _transferContext;
        WorkerThreadManager* _workerThreadManager;
    };
}
