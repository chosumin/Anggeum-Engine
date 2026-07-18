#pragma once

namespace Core
{
	class RenderContext;

	// Draws the engine's runtime status window: frame rate plus the GPU queue
	// timing breakdown.
	class Status
	{
	public:
		explicit Status(RenderContext& renderContext) : _renderContext(renderContext) {}

		void OnGUI();

	private:
		void DrawQueueTimings();

	private:
		RenderContext& _renderContext;
	};
}
