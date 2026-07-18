#include "stdafx.h"
#include "Status.h"
#include "Graphics/RenderContext.h"

using namespace Core;

void Status::OnGUI()
{
	ImGui::Begin("Status");

	ImGuiIO& io = ImGui::GetIO();
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
		1000.0f / io.Framerate, io.Framerate);

	ImGui::SeparatorText("GPU Queue Timing");
	DrawQueueTimings();

	ImGui::End();
}

void Status::DrawQueueTimings()
{
	const auto& timings = _renderContext.GetLastQueueTimings();
	if (timings.valid == false)
	{
		ImGui::TextDisabled("Waiting for GPU timestamps...");
		return;
	}

	ImGui::Text("Graphics busy: %.2f ms", timings.graphicsBusyMs);
	ImGui::Text("Compute busy:  %.2f ms", timings.computeBusyMs);
	ImGui::Text("Overlap:       %.2f ms  (%.1f%% of the shorter queue)",
		timings.overlapMs, timings.overlapPercent);

	ImGui::Spacing();

	// Gantt chart: one lane per queue, sharing a time axis so overlapping bars line
	// up vertically. The frame span is the latest end across all passes.
	double frameSpanMs = 0.0;
	for (const auto& pass : timings.passes)
		frameSpanMs = std::max(frameSpanMs, pass.endMs);
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
