#include "stdafx.h"
#include "FrameGraphResource.h"

namespace Core
{
	FGAccessInfo GetAccessInfo(TextureAccess access)
	{
		FGAccessInfo info{};
		switch (access)
		{
		case TextureAccess::ColorWrite:
			info = { VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true };
			break;
		case TextureAccess::ColorLoadWrite:
			info = { VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true };
			break;
		case TextureAccess::DepthWrite:
			info = { VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
				VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, true };
			break;
		case TextureAccess::DepthLoadWrite:
			info = { VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
				VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, true };
			break;
		case TextureAccess::DepthRead:
			info = { VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
				VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, false };
			break;
		case TextureAccess::SampledFragment:
			info = { VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
				VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false };
			break;
		case TextureAccess::SampledVertex:
			info = { VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
				VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false };
			break;
		case TextureAccess::SampledCompute:
			info = { VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false };
			break;
		case TextureAccess::StorageComputeWrite:
			info = { VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				VK_IMAGE_LAYOUT_GENERAL, true };
			break;
		case TextureAccess::StorageComputeRead:
			info = { VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
				VK_IMAGE_LAYOUT_GENERAL, false };
			break;
		case TextureAccess::TransferSrc:
			info = { VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
				VK_ACCESS_2_TRANSFER_READ_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, false };
			break;
		case TextureAccess::TransferDst:
			info = { VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true };
			break;
		case TextureAccess::Present:
			info = { VK_PIPELINE_STAGE_2_NONE,
				VK_ACCESS_2_NONE,
				VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, false };
			break;
		default:
			assert(false && "unhandled TextureAccess");
			break;
		}
		return info;
	}

	FGAccessInfo GetAccessInfo(BufferAccess access)
	{
		FGAccessInfo info{};
		switch (access)
		{
		case BufferAccess::UniformVertex:
			info = { VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, false };
			break;
		case BufferAccess::UniformFragment:
			info = { VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, false };
			break;
		case BufferAccess::UniformCompute:
			info = { VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, false };
			break;
		case BufferAccess::StorageVertexRead:
			info = { VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, false };
			break;
		case BufferAccess::StorageFragmentRead:
			info = { VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, false };
			break;
		case BufferAccess::StorageComputeRead:
			info = { VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, false };
			break;
		case BufferAccess::StorageComputeWrite:
			info = { VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, true };
			break;
		case BufferAccess::IndirectRead:
			info = { VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, false };
			break;
		case BufferAccess::TransferSrc:
			info = { VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, false };
			break;
		case BufferAccess::TransferDst:
			info = { VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, true };
			break;
		default:
			assert(false && "unhandled BufferAccess");
			break;
		}
		return info;
	}
}
