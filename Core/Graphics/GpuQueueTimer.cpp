#include "stdafx.h"
#include "GpuQueueTimer.h"
#include "Vulkans/Device.h"
#include "Vulkans/CommandBuffer.h"
using namespace Core;

namespace
{
	uint64_t ValidBitsMask(uint32_t validBits)
	{
		if (validBits == 0)
			return 0;
		if (validBits >= 64)
			return ~0ull;

		return (1ull << validBits) - 1;
	}

	// Total time covered by both interval lists at once. Each list is already sorted
	// and non-overlapping, because a queue runs its command buffers in order.
	double IntersectionMs(const vector<pair<double, double>>& a,
		const vector<pair<double, double>>& b)
	{
		double total = 0.0;
		size_t i = 0;
		size_t j = 0;

		while (i < a.size() && j < b.size())
		{
			const double begin = std::max(a[i].first, b[j].first);
			const double end = std::min(a[i].second, b[j].second);
			if (end > begin)
				total += end - begin;

			// Advance whichever interval ends first; the other may still overlap the
			// next one.
			if (a[i].second < b[j].second)
				i++;
			else
				j++;
		}

		return total;
	}
}

GpuQueueTimer::GpuQueueTimer(Device& device)
	: _device(device)
{
	VkPhysicalDeviceProperties properties{};
	vkGetPhysicalDeviceProperties(_device.GetPhysicalDevice(), &properties);
	_timestampPeriod = properties.limits.timestampPeriod;

	// A period of zero means the device cannot write timestamps at all.
	if (_timestampPeriod == 0.0f)
	{
		cout << "[GPU TIMER] Disabled: device does not support timestamps." << endl;
		return;
	}

	uint32_t familyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(_device.GetPhysicalDevice(), &familyCount, nullptr);

	vector<VkQueueFamilyProperties> families(familyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(_device.GetPhysicalDevice(), &familyCount, families.data());

	const auto& queueFamilyIndices = _device.GetQueueFamilyIndices();
	const uint32_t graphicsValidBits = families[queueFamilyIndices.GraphicsFamily.value()].timestampValidBits;
	const uint32_t computeValidBits = families[queueFamilyIndices.ComputeFamily.value()].timestampValidBits;

	if (graphicsValidBits == 0 || computeValidBits == 0)
	{
		cout << "[GPU TIMER] Disabled: a queue family does not support timestamps." << endl;
		return;
	}

	_graphicsMask = ValidBitsMask(graphicsValidBits);
	_computeMask = ValidBitsMask(computeValidBits);

	VkQueryPoolCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	createInfo.queryCount = QueriesPerFrame * MAX_FRAMES_IN_FLIGHT;

	if (vkCreateQueryPool(_device.GetDevice(), &createInfo, nullptr, &_queryPool) != VK_SUCCESS)
		throw runtime_error("failed to create timestamp query pool!");

	_supported = true;
}

GpuQueueTimer::~GpuQueueTimer()
{
	if (_queryPool != VK_NULL_HANDLE)
		vkDestroyQueryPool(_device.GetDevice(), _queryPool, nullptr);
}

void GpuQueueTimer::BeginPass(CommandBuffer& commandBuffer, uint32_t frameIndex,
	uint32_t passIndex, QueueType queue, const char* name)
{
	if (_supported == false || passIndex >= MaxPasses)
		return;

	const uint32_t query = BeginQuery(frameIndex, passIndex);

	// Each pass resets only its own two queries, so passes on different queues never
	// touch the same range and the resets need no cross-queue ordering.
	vkCmdResetQueryPool(commandBuffer.GetHandle(), _queryPool, query, QueriesPerPass);

	// BOTTOM_OF_PIPE rather than TOP_OF_PIPE: a submit's semaphore wait only blocks
	// from its waitDstStageMask onward, so a TOP_OF_PIPE timestamp can be written
	// while the queue is still stalled — which would hide the stall and make an
	// idle queue look busy.
	vkCmdWriteTimestamp(commandBuffer.GetHandle(),
		VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, _queryPool, query);

	auto& record = _records[frameIndex];
	record.queues[passIndex] = queue;
	record.names[passIndex] = name;
	record.passCount = passIndex + 1;
}

void GpuQueueTimer::EndPass(CommandBuffer& commandBuffer, uint32_t frameIndex, uint32_t passIndex)
{
	if (_supported == false || passIndex >= MaxPasses)
		return;

	vkCmdWriteTimestamp(commandBuffer.GetHandle(),
		VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, _queryPool, BeginQuery(frameIndex, passIndex) + 1);
}

QueueTimings GpuQueueTimer::Resolve(uint32_t frameIndex)
{
	QueueTimings timings;

	const auto& record = _records[frameIndex];
	if (_supported == false || record.passCount == 0)
		return timings;

	const uint32_t queryCount = record.passCount * QueriesPerPass;
	vector<uint64_t> ticks(queryCount);

	// No WAIT bit: the caller only resolves a slot whose work it has already waited
	// on, and a not-yet-available result should be skipped rather than stall us.
	VkResult result = vkGetQueryPoolResults(
		_device.GetDevice(), _queryPool,
		BeginQuery(frameIndex, 0), queryCount,
		ticks.size() * sizeof(uint64_t), ticks.data(), sizeof(uint64_t),
		VK_QUERY_RESULT_64_BIT);

	if (result != VK_SUCCESS)
		return timings;

	auto maskFor = [this](QueueType queue)
	{
		return (queue == QueueType::Compute) ? _computeMask : _graphicsMask;
	};

	// Rebase onto a shared origin — both queues time against the same device clock.
	uint64_t origin = ~0ull;
	for (uint32_t pass = 0; pass < record.passCount; pass++)
		origin = std::min(origin, ticks[pass * QueriesPerPass] & maskFor(record.queues[pass]));

	auto toMilliseconds = [this](uint64_t value)
	{
		return static_cast<double>(value) * static_cast<double>(_timestampPeriod) * 1e-6;
	};

	vector<pair<double, double>> graphicsIntervals;
	vector<pair<double, double>> computeIntervals;

	for (uint32_t pass = 0; pass < record.passCount; pass++)
	{
		const QueueType queue = record.queues[pass];
		const uint64_t mask = maskFor(queue);

		const uint64_t begin = ticks[pass * QueriesPerPass] & mask;
		const uint64_t end = ticks[pass * QueriesPerPass + 1] & mask;

		// The masked counter wrapped mid-frame; the whole frame is unusable.
		if (end < begin || begin < origin)
			return timings;

		PassTiming passTiming;
		passTiming.name = record.names[pass];
		passTiming.queue = queue;
		passTiming.beginMs = toMilliseconds(begin - origin);
		passTiming.endMs = toMilliseconds(end - origin);
		timings.passes.push_back(passTiming);

		auto& intervals = (queue == QueueType::Compute) ? computeIntervals : graphicsIntervals;
		intervals.emplace_back(passTiming.beginMs, passTiming.endMs);
	}

	for (const auto& interval : graphicsIntervals)
		timings.graphicsBusyMs += interval.second - interval.first;
	for (const auto& interval : computeIntervals)
		timings.computeBusyMs += interval.second - interval.first;

	timings.overlapMs = IntersectionMs(graphicsIntervals, computeIntervals);

	const double shorter = std::min(timings.graphicsBusyMs, timings.computeBusyMs);
	timings.overlapPercent = shorter > 0.0 ? (timings.overlapMs / shorter) * 100.0 : 0.0;
	timings.valid = true;

	return timings;
}
