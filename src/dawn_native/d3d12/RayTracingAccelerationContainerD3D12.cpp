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

#include "dawn_native/d3d12/RayTracingAccelerationContainerD3D12.h"

#include "common/Assert.h"
#include "dawn_native/d3d12/D3D12Error.h"
#include "dawn_native/d3d12/DeviceD3D12.h"
#include "dawn_native/d3d12/UtilsD3D12.h"

namespace dawn_native { namespace d3d12 {

    namespace {

    }  // anonymous namespace

    // static
    ResultOrError<RayTracingAccelerationContainer*> RayTracingAccelerationContainer::Create(
        Device* device,
        const RayTracingAccelerationContainerDescriptor* descriptor) {
        std::unique_ptr<RayTracingAccelerationContainer> container =
            std::make_unique<RayTracingAccelerationContainer>(device, descriptor);
        DAWN_TRY(container->Initialize(descriptor));
        return container.release();
    }

    void RayTracingAccelerationContainer::DestroyImpl() {
    }

    MaybeError RayTracingAccelerationContainer::Initialize(
        const RayTracingAccelerationContainerDescriptor* descriptor) {

        // acceleration container holds geometry
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Bottom) {
            mGeometries.reserve(descriptor->geometryCount);
            for (unsigned int ii = 0; ii < descriptor->geometryCount; ++ii) {
                const RayTracingAccelerationGeometryDescriptor& geometry =
                    descriptor->geometries[ii];
                D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc;
                geometryDesc.Type = ToD3D12RayTracingGeometryType(geometry.type);
                geometryDesc.Flags = ToD3D12RayTracingGeometryFlags(geometry.flags);

                geometryInfo.geometryType = ToVulkanGeometryType(geometry.type);
                // reset
                geometryInfo.geometry.triangles.sType = VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV;
                geometryInfo.geometry.triangles.pNext = nullptr;
                geometryInfo.geometry.triangles.transformData = nullptr;
                geometryInfo.geometry.triangles.transformOffset = 0;
                geometryInfo.geometry.aabbs.sType = VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV;
                geometryInfo.geometry.aabbs.pNext = nullptr;
                // vertex buffer
                if (geometry.vertex != nullptr && geometry.vertex->buffer != nullptr) {
                    Buffer* vertexBuffer = ToBackend(geometry.vertex->buffer);
                    geometryInfo.geometry.triangles.vertexData = vertexBuffer->GetHandle();
                    geometryInfo.geometry.triangles.vertexOffset = geometry.vertex->offset;
                    geometryInfo.geometry.triangles.vertexCount = geometry.vertex->count;
                    geometryInfo.geometry.triangles.vertexStride = geometry.vertex->stride;
                    geometryInfo.geometry.triangles.vertexFormat =
                        ToVulkanVertexFormat(geometry.vertex->format);
                } else {
                    geometryInfo.geometry.triangles.vertexData = VK_NULL_HANDLE;
                    geometryInfo.geometry.triangles.vertexOffset = 0;
                    geometryInfo.geometry.triangles.vertexCount = 0;
                    geometryInfo.geometry.triangles.vertexStride = 0;
                    geometryInfo.geometry.triangles.vertexFormat = VK_FORMAT_UNDEFINED;
                }
                // index buffer
                if (geometry.index != nullptr && geometry.index->buffer != nullptr) {
                    Buffer* indexBuffer = ToBackend(geometry.index->buffer);
                    geometryInfo.geometry.triangles.indexData = indexBuffer->GetHandle();
                    geometryInfo.geometry.triangles.indexOffset = geometry.index->offset;
                    geometryInfo.geometry.triangles.indexCount = geometry.index->count;
                    geometryInfo.geometry.triangles.indexType =
                        ToVulkanIndexFormat(geometry.index->format);
                } else {
                    geometryInfo.geometry.triangles.indexData = VK_NULL_HANDLE;
                    geometryInfo.geometry.triangles.indexOffset = 0;
                    geometryInfo.geometry.triangles.indexCount = 0;
                    geometryInfo.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_NV;
                }
                // aabb buffer
                if (geometry.aabb != nullptr && geometry.aabb->buffer != nullptr) {
                    Buffer* aabbBuffer = ToBackend(geometry.aabb->buffer);
                    geometryInfo.geometry.aabbs.aabbData = aabbBuffer->GetHandle();
                    geometryInfo.geometry.aabbs.numAABBs = geometry.aabb->count;
                    geometryInfo.geometry.aabbs.stride = geometry.aabb->stride;
                    geometryInfo.geometry.aabbs.offset = geometry.aabb->offset;
                } else {
                    geometryInfo.geometry.aabbs.aabbData = VK_NULL_HANDLE;
                    geometryInfo.geometry.aabbs.numAABBs = 0;
                    geometryInfo.geometry.aabbs.stride = 0;
                    geometryInfo.geometry.aabbs.offset = 0;
                }
                mGeometries.push_back(geometryInfo);
            };
        }

        return {};
    }

    RayTracingAccelerationContainer::~RayTracingAccelerationContainer() {
        DestroyInternal();
    }

    MaybeError RayTracingAccelerationContainer::UpdateInstanceImpl(
        uint32_t instanceIndex,
        const RayTracingAccelerationInstanceDescriptor* descriptor) {
        return {};
    }

}}  // namespace dawn_native::d3d12