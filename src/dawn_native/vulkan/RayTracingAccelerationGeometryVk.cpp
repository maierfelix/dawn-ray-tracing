// Copyright 2018 The Dawn Authors
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

#include "dawn_native/vulkan/RayTracingAccelerationGeometryVk.h"

#include "dawn_native/vulkan/DeviceVk.h"
#include "dawn_native/vulkan/VulkanError.h"

namespace dawn_native { namespace vulkan {

    namespace {

        VkGeometryTypeNV VulkanGeometryType(wgpu::RayTracingAccelerationGeometryType geometryType) {
            switch (geometryType) {
                case wgpu::RayTracingAccelerationGeometryType::Triangles:
                    return VK_GEOMETRY_TYPE_TRIANGLES_NV;
                case wgpu::RayTracingAccelerationGeometryType::Aabbs:
                    return VK_GEOMETRY_TYPE_AABBS_NV;
                default:
                    UNREACHABLE();
            }
        }

        VkIndexType VulkanIndexFormat(wgpu::IndexFormat format) {
            switch (format) {
                case wgpu::IndexFormat::Uint16:
                    return VK_INDEX_TYPE_UINT16;
                case wgpu::IndexFormat::Uint32:
                    return VK_INDEX_TYPE_UINT32;
                case wgpu::IndexFormat::None:
                    return VK_INDEX_TYPE_NONE_NV;
                default:
                    UNREACHABLE();
            }
        }

        VkFormat VulkanVertexFormat(wgpu::VertexFormat format) {
            switch (format) {
                case wgpu::VertexFormat::UChar2:
                    return VK_FORMAT_R8G8_UINT;
                case wgpu::VertexFormat::UChar4:
                    return VK_FORMAT_R8G8B8A8_UINT;
                case wgpu::VertexFormat::Char2:
                    return VK_FORMAT_R8G8_SINT;
                case wgpu::VertexFormat::Char4:
                    return VK_FORMAT_R8G8B8A8_SINT;
                case wgpu::VertexFormat::UChar2Norm:
                    return VK_FORMAT_R8G8_UNORM;
                case wgpu::VertexFormat::UChar4Norm:
                    return VK_FORMAT_R8G8B8A8_UNORM;
                case wgpu::VertexFormat::Char2Norm:
                    return VK_FORMAT_R8G8_SNORM;
                case wgpu::VertexFormat::Char4Norm:
                    return VK_FORMAT_R8G8B8A8_SNORM;
                case wgpu::VertexFormat::UShort2:
                    return VK_FORMAT_R16G16_UINT;
                case wgpu::VertexFormat::UShort4:
                    return VK_FORMAT_R16G16B16A16_UINT;
                case wgpu::VertexFormat::Short2:
                    return VK_FORMAT_R16G16_SINT;
                case wgpu::VertexFormat::Short4:
                    return VK_FORMAT_R16G16B16A16_SINT;
                case wgpu::VertexFormat::UShort2Norm:
                    return VK_FORMAT_R16G16_UNORM;
                case wgpu::VertexFormat::UShort4Norm:
                    return VK_FORMAT_R16G16B16A16_UNORM;
                case wgpu::VertexFormat::Short2Norm:
                    return VK_FORMAT_R16G16_SNORM;
                case wgpu::VertexFormat::Short4Norm:
                    return VK_FORMAT_R16G16B16A16_SNORM;
                case wgpu::VertexFormat::Half2:
                    return VK_FORMAT_R16G16_SFLOAT;
                case wgpu::VertexFormat::Half4:
                    return VK_FORMAT_R16G16B16A16_SFLOAT;
                case wgpu::VertexFormat::Float:
                    return VK_FORMAT_R32_SFLOAT;
                case wgpu::VertexFormat::Float2:
                    return VK_FORMAT_R32G32_SFLOAT;
                case wgpu::VertexFormat::Float3:
                    return VK_FORMAT_R32G32B32_SFLOAT;
                case wgpu::VertexFormat::Float4:
                    return VK_FORMAT_R32G32B32A32_SFLOAT;
                case wgpu::VertexFormat::UInt:
                    return VK_FORMAT_R32_UINT;
                case wgpu::VertexFormat::UInt2:
                    return VK_FORMAT_R32G32_UINT;
                case wgpu::VertexFormat::UInt3:
                    return VK_FORMAT_R32G32B32_UINT;
                case wgpu::VertexFormat::UInt4:
                    return VK_FORMAT_R32G32B32A32_UINT;
                case wgpu::VertexFormat::Int:
                    return VK_FORMAT_R32_SINT;
                case wgpu::VertexFormat::Int2:
                    return VK_FORMAT_R32G32_SINT;
                case wgpu::VertexFormat::Int3:
                    return VK_FORMAT_R32G32B32_SINT;
                case wgpu::VertexFormat::Int4:
                    return VK_FORMAT_R32G32B32A32_SINT;
                default:
                    UNREACHABLE();
            }
        }

        uint8_t VulkanIndexFormatStride(wgpu::IndexFormat indexFormat) {
            switch (indexFormat) {
                case wgpu::IndexFormat::Uint16:
                    return sizeof(uint16_t);
                case wgpu::IndexFormat::Uint32:
                    return sizeof(uint32_t);
                case wgpu::IndexFormat::None:
                    return 1;
                default:
                    UNREACHABLE();
            }
        }

    }  // anonymous namespace

    // static
    ResultOrError<RayTracingAccelerationGeometry*> RayTracingAccelerationGeometry::Create(Device* device, const RayTracingAccelerationGeometryDescriptor* descriptor) {
        std::unique_ptr<RayTracingAccelerationGeometry> geometry = std::make_unique<RayTracingAccelerationGeometry>(device, descriptor);
        DAWN_TRY(geometry->Initialize(descriptor));
        return geometry.release();
    }

    MaybeError RayTracingAccelerationGeometry::Initialize(const RayTracingAccelerationGeometryDescriptor* descriptor) {
        Buffer* vertexBuffer = ToBackend(descriptor->vertexBuffer);
        mGeometryInfo.geometryType = VulkanGeometryType(descriptor->type);

        // for now, we lock the geometry type to triangle-only
        if (descriptor->type != wgpu::RayTracingAccelerationGeometryType::Triangles) {
            return DAWN_VALIDATION_ERROR("Other Geometry types than 'Triangles' is unsupported");
        }

        mGeometryInfo.geometry.triangles.vertexData = vertexBuffer->GetHandle();
        mGeometryInfo.geometry.triangles.vertexOffset = descriptor->vertexOffset;
        mGeometryInfo.geometry.triangles.vertexCount = vertexBuffer->GetSize() / descriptor->vertexStride;
        mGeometryInfo.geometry.triangles.vertexStride = descriptor->vertexStride;
        mGeometryInfo.geometry.triangles.vertexFormat = VulkanVertexFormat(descriptor->vertexFormat);

        // index buffer is optional
        if (descriptor->indexBuffer != nullptr && descriptor->indexFormat != wgpu::IndexFormat::None) {
            Buffer* indexBuffer = ToBackend(descriptor->indexBuffer);
            mGeometryInfo.geometry.triangles.indexData = indexBuffer->GetHandle();
            mGeometryInfo.geometry.triangles.indexOffset = descriptor->indexOffset;
            mGeometryInfo.geometry.triangles.indexType = VulkanIndexFormat(descriptor->indexFormat);
            mGeometryInfo.geometry.triangles.indexCount = indexBuffer->GetSize() / VulkanIndexFormatStride(descriptor->indexFormat);
        }

        mGeometryInfo.geometry.triangles.transformData = nullptr;
        mGeometryInfo.geometry.triangles.transformOffset = 0;
        mGeometryInfo.flags = VK_GEOMETRY_OPAQUE_BIT_NV;

        return {};
    }

    RayTracingAccelerationGeometry::~RayTracingAccelerationGeometry() {

    }

    VkGeometryNV RayTracingAccelerationGeometry::GetInfo() const {
        return mGeometryInfo;
    }

}}  // namespace dawn_native::vulkan
