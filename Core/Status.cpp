#include "stdafx.h"
#include "Status.h"
#include "Graphics/RenderContext.h"

using namespace Core;

void Status::OnGUI()
{
	ImGui::Begin("Status");

	ImGuiIO& io = ImGui::GetIO();
	const float cpuFrameMs = 1000.0f / io.Framerate;
	ImGui::Text("CPU frame:     %.2f ms (%.1f FPS)", cpuFrameMs, io.Framerate);

	const auto& timings = _renderContext.GetLastQueueTimings();
	if (timings.valid == false)
	{
		ImGui::TextDisabled("Waiting for GPU timestamps...");
		ImGui::End();
		return;
	}

	ImGui::Text("GPU frame:     %.2f ms", timings.frameSpanMs);

	DrawCpuPhases();
	DrawQueueTimings();

	ImGui::End();
}

void Status::DrawCpuPhases()
{
	ImGui::SeparatorText("CPU Frame Breakdown");

	const auto& phases = _cpuPhases;
	const double total = phases.transferWaitMs + phases.beginMs + phases.guiMs +
		phases.recordMs + phases.submitMs;

	ImGui::Text("Transfer wait: %.2f ms", phases.transferWaitMs);
	ImGui::Text("Begin frame:   %.2f ms", phases.beginMs);
	ImGui::Text("Build GUI:     %.2f ms", phases.guiMs);
	ImGui::Text("Record passes: %.2f ms", phases.recordMs);
	ImGui::Text("Submit:        %.2f ms", phases.submitMs);
	ImGui::Text("  vkQueueSubmit: %.2f ms", _renderContext.GetLastQueueSubmitMs());
	ImGui::Text("  present:       %.2f ms", _renderContext.GetLastPresentMs());
	ImGui::Separator();
	ImGui::Text("Measured total: %.2f ms", total);
}

void Status::DrawQueueTimings()
{
	ImGui::SeparatorText("GPU Queue Timing");

	const auto& timings = _renderContext.GetLastQueueTimings();

	ImGui::Text("Graphics busy: %.2f ms  (idle %.2f ms)",
		timings.graphicsBusyMs, timings.frameSpanMs - timings.graphicsBusyMs);
	ImGui::Text("Compute busy:  %.2f ms  (idle %.2f ms)",
		timings.computeBusyMs, timings.frameSpanMs - timings.computeBusyMs);
	ImGui::Text("Overlap:       %.2f ms  (%.1f%% of the shorter queue)",
		timings.overlapMs, timings.overlapPercent);

	ImGui::Spacing();

	// Gantt chart: one lane per queue, sharing a time axis so overlapping bars line
	// up vertically.
	double frameSpanMs = timings.frameSpanMs;
	if (frameSpanMs <= 0.0)
		frameSpanMs = 1.0;

	const ImU32 graphicsColor = IM_COL32(90, 160, 240, 255);
	const ImU32 computeColor = IM_COL32(240, 170, 80, 255);
	const ImU32 laneBackground = IM_COL32(40, 40, 40, 255);
	const ImU32 labelColor = IM_COL32(255, 255, 255, 255);

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const float fullWidth = ImGui::GetContentRegionAvail().x;
	const float laneHeight = ImGui::GetTextLineHeight() + 6.0f;
	const float pixelsPerMs = fullWidth / static_cast<float>(frameSpanMs);

	auto drawLane = [&](const char* laneLabel, QueueType queue, ImU32 barColor)
	{
		ImGui::TextUnformatted(laneLabel);

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		drawList->AddRectFilled(origin,
			ImVec2(origin.x + fullWidth, origin.y + laneHeight), laneBackground);

		for (const auto& pass : timings.passes)
		{
			if (pass.queue != queue)
				continue;

			const float x0 = origin.x + static_cast<float>(pass.beginMs) * pixelsPerMs;
			const float x1 = origin.x + static_cast<float>(pass.endMs) * pixelsPerMs;

			drawList->AddRectFilled(
				ImVec2(x0, origin.y + 1.0f),
				ImVec2(std::max(x1, x0 + 1.0f), origin.y + laneHeight - 1.0f), barColor);

			// Only label bars wide enough to fit readable text.
			if (x1 - x0 > 40.0f)
				drawList->AddText(ImVec2(x0 + 3.0f, origin.y + 3.0f), labelColor, pass.name);
		}

		ImGui::Dummy(ImVec2(fullWidth, laneHeight));
	};

	drawLane("Graphics", QueueType::Graphics, graphicsColor);
	drawLane("Compute", QueueType::Compute, computeColor);

	ImGui::Spacing();

	// Per-pass detail table.
	if (ImGui::BeginTable("passes", 4,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Queue");
		ImGui::TableSetupColumn("Pass");
		ImGui::TableSetupColumn("Range (ms)");
		ImGui::TableSetupColumn("Duration");
		ImGui::TableHeadersRow();

		for (const auto& pass : timings.passes)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(pass.queue == QueueType::Compute ? "Compute" : "Graphics");
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(pass.name);
			ImGui::TableNextColumn();
			ImGui::Text("%.3f - %.3f", pass.beginMs, pass.endMs);
			ImGui::TableNextColumn();
			ImGui::Text("%.3f", pass.DurationMs());
		}

		ImGui::EndTable();
	}
}
