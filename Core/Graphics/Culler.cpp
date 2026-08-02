#include "Graphics/Culler.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/FrameResources.h"

using namespace Core;

Core::Culler::Culler(Device& device, RendererBatch& rendererBatch, uint32_t id)
    : _device(device)
    , _rendererBatch(rendererBatch)
    // Per-frame unique prefix so multiple cullers (e.g. main camera + shadow
    // cascades) get distinct FrameResources entries instead of sharing.
    , _namePrefix("Culler" + std::to_string(id) + ".")
{
}

Core::Culler::~Culler() = default;

void Core::Culler::PrepareBatchResources(FrameResources& frameResources)
{
    _instanceCount = _rendererBatch.GetInstanceCount();
    _drawCount = _rendererBatch.GetIndirectDrawBuffer().GetDrawCount();
    _batchRevision = _rendererBatch.GetRevision();

    // The indirect command buffer starts out holding the full draw list. The
    // culling shaders only rewrite the instance counts, so the rest of each command
    // comes from this fill and has to be refreshed whenever the draw set changes.
    const auto& drawCommands = _rendererBatch.GetIndirectDrawBuffer().GetDrawCommands();

    BufferDesc indirectDesc{};
    indirectDesc.size = drawCommands.size() * sizeof(DrawIndexedIndirectCommand);
    indirectDesc.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    indirectDesc.memoryType = MemoryType::DEVICE_LOCAL;

    _indirectCommandBuffer = frameResources.CreateOrReplaceStorageBuffer(
        _namePrefix + "Pass1Indirect", indirectDesc, drawCommands);
}

void Core::Culler::OnBatchRebuilt(FrameResources& frameResources)
{
    PrepareBatchResources(frameResources);
}

void Core::Culler::ExtractFrustumPlanes(const glm::mat4& viewProj, glm::vec4* planes)
{
    // Left
    planes[0] = glm::vec4(
        viewProj[0][3] + viewProj[0][0],
        viewProj[1][3] + viewProj[1][0],
        viewProj[2][3] + viewProj[2][0],
        viewProj[3][3] + viewProj[3][0]);

    // Right
    planes[1] = glm::vec4(
        viewProj[0][3] - viewProj[0][0],
        viewProj[1][3] - viewProj[1][0],
        viewProj[2][3] - viewProj[2][0],
        viewProj[3][3] - viewProj[3][0]);

    // Bottom
    planes[2] = glm::vec4(
        viewProj[0][3] + viewProj[0][1],
        viewProj[1][3] + viewProj[1][1],
        viewProj[2][3] + viewProj[2][1],
        viewProj[3][3] + viewProj[3][1]);

    // Top
    planes[3] = glm::vec4(
        viewProj[0][3] - viewProj[0][1],
        viewProj[1][3] - viewProj[1][1],
        viewProj[2][3] - viewProj[2][1],
        viewProj[3][3] - viewProj[3][1]);

    // Near
    planes[4] = glm::vec4(
        viewProj[0][3] + viewProj[0][2],
        viewProj[1][3] + viewProj[1][2],
        viewProj[2][3] + viewProj[2][2],
        viewProj[3][3] + viewProj[3][2]);

    // Far
    planes[5] = glm::vec4(
        viewProj[0][3] - viewProj[0][2],
        viewProj[1][3] - viewProj[1][2],
        viewProj[2][3] - viewProj[2][2],
        viewProj[3][3] - viewProj[3][2]);

    // Normalize planes
    for (int i = 0; i < 6; ++i)
    {
        float length = glm::length(glm::vec3(planes[i]));
        planes[i] /= length;
    }
}
