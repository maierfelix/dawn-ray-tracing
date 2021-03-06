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

#ifndef DAWNNATIVE_D3D12_UTILSD3D12_H_
#define DAWNNATIVE_D3D12_UTILSD3D12_H_

#include "dawn_native/d3d12/BufferD3D12.h"
#include "dawn_native/d3d12/TextureCopySplitter.h"
#include "dawn_native/d3d12/TextureD3D12.h"
#include "dawn_native/d3d12/d3d12_platform.h"
#include "dawn_native/dawn_platform.h"

namespace dawn_native { namespace d3d12 {

    ResultOrError<std::wstring> ConvertStringToWstring(const char* str);

    D3D12_COMPARISON_FUNC ToD3D12ComparisonFunc(wgpu::CompareFunction func);

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE ToD3D12RayTracingAccelerationContainerLevel(
        wgpu::RayTracingAccelerationContainerLevel level);
    D3D12_HIT_GROUP_TYPE ToD3D12ShaderBindingTableGroupType(
        wgpu::RayTracingShaderBindingTableGroupType type);
    D3D12_RAYTRACING_GEOMETRY_TYPE ToD3D12RayTracingGeometryType(
        wgpu::RayTracingAccelerationGeometryType geometryType);
    DXGI_FORMAT ToD3D12RayTracingAccelerationContainerIndexFormat(wgpu::IndexFormat format);
    DXGI_FORMAT ToD3D12RayTracingAccelerationContainerVertexFormat(wgpu::VertexFormat format);
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS
    ToD3D12RayTracingAccelerationStructureBuildFlags(
        wgpu::RayTracingAccelerationContainerUsage buildUsage);
    D3D12_RAYTRACING_INSTANCE_FLAGS ToD3D12RayTracingInstanceFlags(
        wgpu::RayTracingAccelerationInstanceUsage instanceUsage);
    D3D12_RAYTRACING_GEOMETRY_FLAGS ToD3D12RayTracingGeometryFlags(
        wgpu::RayTracingAccelerationGeometryUsage geometryUsage);

    D3D12_TEXTURE_COPY_LOCATION ComputeTextureCopyLocationForTexture(const Texture* texture,
                                                                     uint32_t level,
                                                                     uint32_t slice);
    D3D12_TEXTURE_COPY_LOCATION ComputeBufferLocationForCopyTextureRegion(
        const Texture* texture,
        ID3D12Resource* bufferResource,
        const Extent3D& bufferSize,
        const uint64_t offset,
        const uint32_t rowPitch);
    D3D12_BOX ComputeD3D12BoxFromOffsetAndSize(const Origin3D& offset, const Extent3D& copySize);

}}  // namespace dawn_native::d3d12

#endif  // DAWNNATIVE_D3D12_UTILSD3D12_H_
