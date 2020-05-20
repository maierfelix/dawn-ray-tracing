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

#include "dawn_native/d3d12/UtilsD3D12.h"

#include "common/Assert.h"

#include <stringapiset.h>

namespace dawn_native { namespace d3d12 {

    ResultOrError<std::wstring> ConvertStringToWstring(const char* str) {
        size_t len = strlen(str);
        if (len == 0) {
            return std::wstring();
        }
        int numChars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str, len, nullptr, 0);
        if (numChars == 0) {
            return DAWN_INTERNAL_ERROR("Failed to convert string to wide string");
        }
        std::wstring result;
        result.resize(numChars);
        int numConvertedChars =
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str, len, &result[0], numChars);
        if (numConvertedChars != numChars) {
            return DAWN_INTERNAL_ERROR("Failed to convert string to wide string");
        }
        return std::move(result);
    }

    D3D12_COMPARISON_FUNC ToD3D12ComparisonFunc(wgpu::CompareFunction func) {
        switch (func) {
            case wgpu::CompareFunction::Never:
                return D3D12_COMPARISON_FUNC_NEVER;
            case wgpu::CompareFunction::Less:
                return D3D12_COMPARISON_FUNC_LESS;
            case wgpu::CompareFunction::LessEqual:
                return D3D12_COMPARISON_FUNC_LESS_EQUAL;
            case wgpu::CompareFunction::Greater:
                return D3D12_COMPARISON_FUNC_GREATER;
            case wgpu::CompareFunction::GreaterEqual:
                return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
            case wgpu::CompareFunction::Equal:
                return D3D12_COMPARISON_FUNC_EQUAL;
            case wgpu::CompareFunction::NotEqual:
                return D3D12_COMPARISON_FUNC_NOT_EQUAL;
            case wgpu::CompareFunction::Always:
                return D3D12_COMPARISON_FUNC_ALWAYS;
            default:
                UNREACHABLE();
        }
    }

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE ToD3D12RayTracingAccelerationContainerLevel(
        wgpu::RayTracingAccelerationContainerLevel level) {
        switch (level) {
            case wgpu::RayTracingAccelerationContainerLevel::Bottom:
                return D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            case wgpu::RayTracingAccelerationContainerLevel::Top:
                return D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
            default:
                UNREACHABLE();
        }
    }

    D3D12_HIT_GROUP_TYPE ToD3D12ShaderBindingTableGroupType(
        wgpu::RayTracingShaderBindingTableGroupType type) {
        switch (type) {
            case wgpu::RayTracingShaderBindingTableGroupType::TrianglesHitGroup:
                return D3D12_HIT_GROUP_TYPE_TRIANGLES;
            case wgpu::RayTracingShaderBindingTableGroupType::ProceduralHitGroup:
                return D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE;
            default:
                UNREACHABLE();
        }
    }

    D3D12_RAYTRACING_GEOMETRY_TYPE ToD3D12RayTracingGeometryType(
        wgpu::RayTracingAccelerationGeometryType geometryType) {
        switch (geometryType) {
            case wgpu::RayTracingAccelerationGeometryType::Triangles:
                return D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            case wgpu::RayTracingAccelerationGeometryType::Aabbs:
                return D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
            default:
                UNREACHABLE();
        }
    }

    DXGI_FORMAT ToD3D12RayTracingAccelerationContainerVertexFormat(wgpu::VertexFormat format) {
        switch (format) {
            case wgpu::VertexFormat::Float2:
                return DXGI_FORMAT_R32G32_FLOAT;
            case wgpu::VertexFormat::Float3:
                return DXGI_FORMAT_R32G32B32_FLOAT;
            default:
                UNREACHABLE();
        }
    }

    DXGI_FORMAT ToD3D12RayTracingAccelerationContainerIndexFormat(wgpu::IndexFormat format) {
        switch (format) {
            case wgpu::IndexFormat::None:
                return DXGI_FORMAT_UNKNOWN;
            case wgpu::IndexFormat::Uint16:
                return DXGI_FORMAT_R16_UINT;
            case wgpu::IndexFormat::Uint32:
                return DXGI_FORMAT_R32_UINT;
            default:
                UNREACHABLE();
        }
    }

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS
    ToD3D12RayTracingAccelerationStructureBuildFlags(
        wgpu::RayTracingAccelerationContainerUsage buildUsage) {
        uint32_t flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
        if (buildUsage & wgpu::RayTracingAccelerationContainerUsage::AllowUpdate) {
            flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        }
        if (buildUsage & wgpu::RayTracingAccelerationContainerUsage::PreferFastBuild) {
            flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
        }
        if (buildUsage & wgpu::RayTracingAccelerationContainerUsage::PreferFastTrace) {
            flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        }
        if (buildUsage & wgpu::RayTracingAccelerationContainerUsage::LowMemory) {
            flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_MINIMIZE_MEMORY;
        }
        return static_cast<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS>(flags);
    }

    D3D12_RAYTRACING_INSTANCE_FLAGS ToD3D12RayTracingInstanceFlags(
        wgpu::RayTracingAccelerationInstanceUsage instanceUsage) {
        uint32_t flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        if (instanceUsage & wgpu::RayTracingAccelerationInstanceUsage::TriangleCullDisable) {
            flags |= D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
        }
        if (instanceUsage &
            wgpu::RayTracingAccelerationInstanceUsage::TriangleFrontCounterclockwise) {
            flags |= D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE;
        }
        if (instanceUsage & wgpu::RayTracingAccelerationInstanceUsage::ForceOpaque) {
            flags |= D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
        }
        if (instanceUsage & wgpu::RayTracingAccelerationInstanceUsage::ForceNoOpaque) {
            flags |= D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE;
        }
        return static_cast<D3D12_RAYTRACING_INSTANCE_FLAGS>(flags);
    }

    D3D12_RAYTRACING_GEOMETRY_FLAGS ToD3D12RayTracingGeometryFlags(
        wgpu::RayTracingAccelerationGeometryUsage geometryUsage) {
        uint32_t flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
        if (geometryUsage & wgpu::RayTracingAccelerationGeometryUsage::Opaque) {
            flags |= D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        }
        if (geometryUsage & wgpu::RayTracingAccelerationGeometryUsage::AllowAnyHit) {
            flags |= D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION;
        }
        return static_cast<D3D12_RAYTRACING_GEOMETRY_FLAGS>(flags);
    }

    D3D12_TEXTURE_COPY_LOCATION ComputeTextureCopyLocationForTexture(const Texture* texture,
                                                                     uint32_t level,
                                                                     uint32_t slice) {
        D3D12_TEXTURE_COPY_LOCATION copyLocation;
        copyLocation.pResource = texture->GetD3D12Resource();
        copyLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        copyLocation.SubresourceIndex = texture->GetSubresourceIndex(level, slice);

        return copyLocation;
    }

    D3D12_TEXTURE_COPY_LOCATION ComputeBufferLocationForCopyTextureRegion(
        const Texture* texture,
        ID3D12Resource* bufferResource,
        const Extent3D& bufferSize,
        const uint64_t offset,
        const uint32_t rowPitch) {
        D3D12_TEXTURE_COPY_LOCATION bufferLocation;
        bufferLocation.pResource = bufferResource;
        bufferLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        bufferLocation.PlacedFootprint.Offset = offset;
        bufferLocation.PlacedFootprint.Footprint.Format = texture->GetD3D12Format();
        bufferLocation.PlacedFootprint.Footprint.Width = bufferSize.width;
        bufferLocation.PlacedFootprint.Footprint.Height = bufferSize.height;
        bufferLocation.PlacedFootprint.Footprint.Depth = bufferSize.depth;
        bufferLocation.PlacedFootprint.Footprint.RowPitch = rowPitch;
        return bufferLocation;
    }

    D3D12_BOX ComputeD3D12BoxFromOffsetAndSize(const Origin3D& offset, const Extent3D& copySize) {
        D3D12_BOX sourceRegion;
        sourceRegion.left = offset.x;
        sourceRegion.top = offset.y;
        sourceRegion.front = offset.z;
        sourceRegion.right = offset.x + copySize.width;
        sourceRegion.bottom = offset.y + copySize.height;
        sourceRegion.back = offset.z + copySize.depth;
        return sourceRegion;
    }

}}  // namespace dawn_native::d3d12
