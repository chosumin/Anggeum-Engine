#pragma once
#include "Status.h"

namespace Core
{
    class Timer;
    class Window;
    class Scene;
    class RenderContext;
    class WorkerThreadManager;
    class IRenderPipeline;
    class RenderScene;
    class TransferContext;
    class SyncContext;

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
        WorkerThreadManager* _workerThreadManager;
        
        IRenderPipeline* _renderPipeline;
        RenderContext* _renderContext;

        // Queue-submission authority
        SyncContext* _syncContext;

        // Every render resource upload request funnels through it 
        // (staging, budget and completion tracking live here).
        TransferContext* _transferContext;

        Scene* _scene;

        // GPU mirror of the scene for GPU-driven rendering, synced from scene dirty.
        RenderScene* _renderScene;
    };
}
