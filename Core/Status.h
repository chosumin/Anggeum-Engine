#pragma once

namespace Core
{
	class RenderContext;

	struct CpuPhaseTimings
	{
		double transferWaitMs = 0.0;  // waiting on pending uploads
		double beginMs = 0.0;         // swapchain acquire + frame-slot wait
		double guiMs = 0.0;           // building ImGui draw data
		double recordMs = 0.0;        // recording every pass's command buffers
		double submitMs = 0.0;        // vkQueueSubmit + present
	};

	// Times a scope and adds the elapsed milliseconds into the given field.
	class ScopedCpuTimer
	{
	public:
		explicit ScopedCpuTimer(double& target)
			: _target(target), _start(std::chrono::steady_clock::now()) {}

		~ScopedCpuTimer()
		{
			const std::chrono::duration<double, std::milli> elapsed =
				std::chrono::steady_clock::now() - _start;
			_target = elapsed.count();
		}

	private:
		double& _target;
		std::chrono::steady_clock::time_point _start;
	};

	class Status
	{
	public:
		explicit Status(RenderContext& renderContext) : _renderContext(renderContext) {}

		void OnGUI();

		// Written by Engine each frame as it moves through the phases.
		CpuPhaseTimings& GetCpuPhases() { return _cpuPhases; }

	private:
		void DrawQueueTimings();
		void DrawCpuPhases();

	private:
		RenderContext& _renderContext;
		CpuPhaseTimings _cpuPhases;
	};
}
