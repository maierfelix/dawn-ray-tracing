// Copyright 2019 The Dawn Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "dawn_native/vulkan/UtilsVulkan.h"

#include "common/Assert.h"
#include "dawn_native/Format.h"
#include "dawn_native/vulkan/BufferVk.h"
#include "dawn_native/vulkan/DeviceVk.h"
#include "dawn_native/vulkan/ResourceHeapVk.h"
#include "dawn_native/vulkan/TextureVk.h"
#include "dawn_native/vulkan/VulkanError.h"

namespace dawn_native { namespace vulkan {

    VkCompareOp ToVulkanCompareOp(wgpu::CompareFunction op) {
        switch (op) {
            case wgpu::CompareFunction::Always:
                return VK_COMPARE_OP_ALWAYS;
            case wgpu::CompareFunction::Equal:
                return VK_COMPARE_OP_EQUAL;
            case wgpu::CompareFunction::Greater:
                return VK_COMPARE_OP_GREATER;
            case wgpu::CompareFunction::GreaterEqual:
                return VK_COMPARE_OP_GREATER_OR_EQUAL;
            case wgpu::CompareFunction::Less:
                return VK_COMPARE_OP_LESS;
            case wgpu::CompareFunction::LessEqual:
                return VK_COMPARE_OP_LESS_OR_EQUAL;
            case wgpu::CompareFunction::Never:
                return VK_COMPARE_OP_NEVER;
            case wgpu::CompareFunction::NotEqual:
                return VK_COMPARE_OP_NOT_EQUAL;
            default:
                UNREACHABLE();
        }
    }

    VkShaderStageFlags ToVulkanShaderStageFlags(wgpu::ShaderStage stages) {
        VkShaderStageFlags flags = 0;

        if (stages & wgpu::ShaderStage::Vertex) {
            flags |= VK_SHADER_STAGE_VERTEX_BIT;
        }
        if (stages & wgpu::ShaderStage::Fragment) {
            flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        if (stages & wgpu::ShaderStage::Compute) {
            flags |= VK_SHADER_STAGE_COMPUTE_BIT;
        }
        if (stages & wgpu::ShaderStage::RayGeneration) {
            flags |= VK_SHADER_STAGE_RAYGEN_BIT_NV;
        }
        if (stages & wgpu::ShaderStage::RayClosestHit) {
            flags |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
        }
        if (stages & wgpu::ShaderStage::RayAnyHit) {
            flags |= VK_SHADER_STAGE_ANY_HIT_BIT_NV;
        }
        if (stages & wgpu::ShaderStage::RayMiss) {
            flags |= VK_SHADER_STAGE_MISS_BIT_NV;
        }
        if (stages & wgpu::ShaderStage::RayIntersection) {
            flags |= VK_SHADER_STAGE_INTERSECTION_BIT_NV;
        }

        return flags;
    }

    VkGeometryTypeNV ToVulkanGeometryType(wgpu::RayTracingAccelerationGeometryType geometryType) {
        switch (geometryType) {
            case wgpu::RayTracingAccelerationGeometryType::Triangles:
                return VK_GEOMETRY_TYPE_TRIANGLES_NV;
            case wgpu::RayTracingAccelerationGeometryType::Aabbs:
                return VK_GEOMETRY_TYPE_AABBS_NV;
            default:
                UNREACHABLE();
        }
    }

    VkIndexType ToVulkanIndexFormat(wgpu::IndexFormat format) {
        switch (format) {
            case wgpu::IndexFormat::None:
                return VK_INDEX_TYPE_NONE_NV;
            case wgpu::IndexFormat::Uint16:
                return VK_INDEX_TYPE_UINT16;
            case wgpu::IndexFormat::Uint32:
                return VK_INDEX_TYPE_UINT32;
            default:
                UNREACHABLE();
        }
    }

    VkFormat ToVulkanVertexFormat(wgpu::VertexFormat format) {
        switch (format) {
            case wgpu::VertexFormat::Float2:
                return VK_FORMAT_R32G32_SFLOAT;
            case wgpu::VertexFormat::Float3:
                return VK_FORMAT_R32G32B32_SFLOAT;
            default:
                UNREACHABLE();
        }
    }

    VkAccelerationStructureTypeNV ToVulkanAccelerationContainerLevel(
        wgpu::RayTracingAccelerationContainerLevel level) {
        switch (level) {
            case wgpu::RayTracingAccelerationContainerLevel::Bottom:
                return VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
            case wgpu::RayTracingAccelerationContainerLevel::Top:
                return VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
            default:
                UNREACHABLE();
        }
    }

    VkBuildAccelerationStructureFlagBitsNV ToVulkanBuildAccelerationContainerFlags(
        wgpu::RayTracingAccelerationContainerFlag buildFlags) {
        uint32_t flags = 0;
        if (buildFlags & wgpu::RayTracingAccelerationContainerFlag::AllowUpdate) {
            flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV;
        }
        if (buildFlags & wgpu::RayTracingAccelerationContainerFlag::PreferFastBuild) {
            flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_NV;
        }
        if (buildFlags & wgpu::RayTracingAccelerationContainerFlag::PreferFastTrace) {
            flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV;
        }
        if (buildFlags & wgpu::RayTracingAccelerationContainerFlag::LowMemory) {
            flags |= VK_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_NV;
        }
        return static_cast<VkBuildAccelerationStructureFlagBitsNV>(flags);
    }

    VkGeometryInstanceFlagBitsNV ToVulkanAccelerationContainerInstanceFlags(
        wgpu::RayTracingAccelerationInstanceFlag instanceFlags) {
        uint32_t flags = 0;
        if (instanceFlags & wgpu::RayTracingAccelerationInstanceFlag::TriangleCullDisable) {
            flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV;
        }
        if (instanceFlags &
            wgpu::RayTracingAccelerationInstanceFlag::TriangleFrontCounterclockwise) {
            flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_NV;
        }
        if (instanceFlags & wgpu::RayTracingAccelerationInstanceFlag::ForceOpaque) {
            flags |= VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_NV;
        }
        if (instanceFlags & wgpu::RayTracingAccelerationInstanceFlag::ForceNoOpaque) {
            flags |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_NV;
        }
        return static_cast<VkGeometryInstanceFlagBitsNV>(flags);
    }

    VkGeometryFlagBitsNV ToVulkanAccelerationContainerGeometryFlags(
        wgpu::RayTracingAccelerationGeometryFlag geometryFlags) {
        uint32_t flags = 0;
        if (geometryFlags & wgpu::RayTracingAccelerationGeometryFlag::Opaque) {
            flags |= VK_GEOMETRY_OPAQUE_BIT_NV;
        }
        if (geometryFlags & wgpu::RayTracingAccelerationGeometryFlag::AllowAnyHit) {
            flags |= VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_NV;
        }
        return static_cast<VkGeometryFlagBitsNV>(flags);
    }

    // Vulkan SPEC requires the source/destination region specified by each element of
    // pRegions must be a region that is contained within srcImage/dstImage. Here the size of
    // the image refers to the virtual size, while Dawn validates texture copy extent with the
    // physical size, so we need to re-calculate the texture copy extent to ensure it should fit
    // in the virtual size of the subresource.
    Extent3D ComputeTextureCopyExtent(const TextureCopy& textureCopy, const Extent3D& copySize) {
        Extent3D validTextureCopyExtent = copySize;
        const TextureBase* texture = textureCopy.texture.Get();
        Extent3D virtualSizeAtLevel = texture->GetMipLevelVirtualSize(textureCopy.mipLevel);
        if (textureCopy.origin.x + copySize.width > virtualSizeAtLevel.width) {
            ASSERT(texture->GetFormat().isCompressed);
            validTextureCopyExtent.width = virtualSizeAtLevel.width - textureCopy.origin.x;
        }
        if (textureCopy.origin.y + copySize.height > virtualSizeAtLevel.height) {
            ASSERT(texture->GetFormat().isCompressed);
            validTextureCopyExtent.height = virtualSizeAtLevel.height - textureCopy.origin.y;
        }

        return validTextureCopyExtent;
    }

    VkBufferImageCopy ComputeBufferImageCopyRegion(const BufferCopy& bufferCopy,
                                                   const TextureCopy& textureCopy,
                                                   const Extent3D& copySize) {
        const Texture* texture = ToBackend(textureCopy.texture.Get());

        VkBufferImageCopy region;

        region.bufferOffset = bufferCopy.offset;
        // In Vulkan the row length is in texels while it is in bytes for Dawn
        const Format& format = texture->GetFormat();
        ASSERT(bufferCopy.rowPitch % format.blockByteSize == 0);
        region.bufferRowLength = bufferCopy.rowPitch / format.blockByteSize * format.blockWidth;
        region.bufferImageHeight = bufferCopy.imageHeight;

        region.imageSubresource.aspectMask = texture->GetVkAspectMask();
        region.imageSubresource.mipLevel = textureCopy.mipLevel;
        region.imageSubresource.baseArrayLayer = textureCopy.arrayLayer;
        region.imageSubresource.layerCount = 1;

        region.imageOffset.x = textureCopy.origin.x;
        region.imageOffset.y = textureCopy.origin.y;
        region.imageOffset.z = textureCopy.origin.z;

        Extent3D imageExtent = ComputeTextureCopyExtent(textureCopy, copySize);
        region.imageExtent.width = imageExtent.width;
        region.imageExtent.height = imageExtent.height;
        region.imageExtent.depth = copySize.depth;

        return region;
    }

    MaybeError CreateBufferFromResourceMemoryAllocation(Device* device,
                                                        VkBuffer* buffer,
                                                        uint32_t size,
                                                        VkBufferUsageFlags usage,
                                                        ResourceMemoryAllocation resource) {
        if (size == 0) {
            return DAWN_VALIDATION_ERROR("Invalid Allocation Size: 0 is not a valid size");
        }

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.pNext = nullptr;
        bufferInfo.flags = 0;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufferInfo.queueFamilyIndexCount = 0;
        bufferInfo.pQueueFamilyIndices = nullptr;

        MaybeError result = CheckVkSuccess(
            device->fn.CreateBuffer(device->GetVkDevice(), &bufferInfo, nullptr, buffer),
            "CreateBuffer");
        if (result.IsError())
            return result;

        DAWN_TRY(CheckVkSuccess(
            device->fn.BindBufferMemory(device->GetVkDevice(), *buffer,
                                        ToBackend(resource.GetResourceHeap())->GetMemory(),
                                        resource.GetOffset()),
            "BindBufferMemory"));

        return {};
    }

}}  // namespace dawn_native::vulkan
