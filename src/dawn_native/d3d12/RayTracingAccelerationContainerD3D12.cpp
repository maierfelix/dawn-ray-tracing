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
#include "common/Math.h"
#include "dawn_native/d3d12/D3D12Error.h"
#include "dawn_native/d3d12/DeviceD3D12.h"
#include "dawn_native/d3d12/HeapD3D12.h"
#include "dawn_native/d3d12/UtilsD3D12.h"

namespace dawn_native { namespace d3d12 {

    namespace {

        D3D12_RAYTRACING_INSTANCE_DESC GetD3D12AccelerationInstance(
            const RayTracingAccelerationInstanceDescriptor& descriptor) {
            RayTracingAccelerationContainer* geometryContainer =
                ToBackend(descriptor.geometryContainer);
            ComPtr<ID3D12Resource> resultMemory =
                geometryContainer->GetScratchMemory().result.resource.GetD3D12Resource();
            D3D12_RAYTRACING_INSTANCE_DESC out;
            // process transform object
            if (descriptor.transform != nullptr) {
                float transform[16] = {};
                const Transform3DDescriptor* tr = descriptor.transform->translation;
                const Transform3DDescriptor* ro = descriptor.transform->rotation;
                const Transform3DDescriptor* sc = descriptor.transform->scale;
                Fill4x3TransformMatrix(transform, tr->x, tr->y, tr->z, ro->x, ro->y, ro->z, sc->x,
                                       sc->y, sc->z);
                memcpy(&out.Transform, transform, sizeof(out.Transform));
            }
            // process transform matrix
            else if (descriptor.transformMatrix != nullptr) {
                memcpy(&out.Transform, descriptor.transformMatrix, sizeof(out.Transform));
            }
            out.InstanceID = descriptor.instanceId;
            out.InstanceMask = descriptor.mask;
            out.InstanceContributionToHitGroupIndex = descriptor.instanceOffset;
            out.Flags = ToD3D12RayTracingInstanceFlags(descriptor.usage);
            out.AccelerationStructure = resultMemory.Get()->GetGPUVirtualAddress();
            return out;
        }

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
        Device* device = ToBackend(GetDevice());
        DestroyScratchBuildMemory();

        if (mScratchMemory.result.buffer != nullptr) {
            device->DeallocateMemory(mScratchMemory.result.resource);
            mScratchMemory.result.buffer = nullptr;
        }
        if (mScratchMemory.update.buffer != nullptr) {
            device->DeallocateMemory(mScratchMemory.update.resource);
            mScratchMemory.update.buffer = nullptr;
        }
        if (mInstanceMemory.buffer != nullptr) {
            Buffer* buffer = mInstanceMemory.allocation.Get();
            if (buffer != nullptr) {
                buffer->Destroy();
            }
            mInstanceMemory.buffer = nullptr;
        }
    }

    MaybeError RayTracingAccelerationContainer::Initialize(
        const RayTracingAccelerationContainerDescriptor* descriptor) {
        Device* device = ToBackend(GetDevice());

        // acceleration container holds geometry
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Bottom) {
            mGeometries.reserve(descriptor->geometryCount);
            for (unsigned int ii = 0; ii < descriptor->geometryCount; ++ii) {
                const RayTracingAccelerationGeometryDescriptor& geometry =
                    descriptor->geometries[ii];
                D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
                geometryDesc.Type = ToD3D12RayTracingGeometryType(geometry.type);
                geometryDesc.Flags = ToD3D12RayTracingGeometryFlags(geometry.usage);

                // vertex buffer
                if (geometry.vertex != nullptr && geometry.vertex->buffer != nullptr) {
                    Buffer* vertexBuffer = ToBackend(geometry.vertex->buffer);
                    geometryDesc.Triangles.VertexBuffer.StartAddress =
                        vertexBuffer->GetD3D12Resource()->GetGPUVirtualAddress() +
                        geometry.vertex->offset;
                    geometryDesc.Triangles.VertexBuffer.StrideInBytes =
                        static_cast<uint64_t>(geometry.vertex->stride);
                    geometryDesc.Triangles.VertexCount = geometry.vertex->count;
                    geometryDesc.Triangles.VertexFormat =
                        ToD3D12RayTracingAccelerationContainerVertexFormat(geometry.vertex->format);
                }
                // index buffer
                if (geometry.index != nullptr && geometry.index->buffer != nullptr) {
                    Buffer* indexBuffer = ToBackend(geometry.index->buffer);
                    geometryDesc.Triangles.IndexBuffer =
                        indexBuffer->GetD3D12Resource()->GetGPUVirtualAddress() +
                        geometry.index->offset;
                    geometryDesc.Triangles.IndexCount = geometry.index->count;
                    geometryDesc.Triangles.IndexFormat =
                        ToD3D12RayTracingAccelerationContainerIndexFormat(geometry.index->format);
                }
                // aabb buffer
                if (geometry.aabb != nullptr && geometry.aabb->buffer != nullptr) {
                    Buffer* aabbBuffer = ToBackend(geometry.aabb->buffer);
                    geometryDesc.AABBs.AABBs.StartAddress =
                        aabbBuffer->GetD3D12Resource()->GetGPUVirtualAddress() +
                        geometry.aabb->offset;
                    geometryDesc.AABBs.AABBCount = geometry.aabb->count;
                    geometryDesc.AABBs.AABBs.StrideInBytes =
                        static_cast<uint64_t>(geometry.aabb->stride);
                }
                mGeometries.push_back(geometryDesc);
            }
        }

        // acceleration container holds instances
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
            mInstances.reserve(descriptor->instanceCount);
            // create data for instance buffer
            for (unsigned int ii = 0; ii < descriptor->instanceCount; ++ii) {
                const RayTracingAccelerationInstanceDescriptor& instance =
                    descriptor->instances[ii];
                D3D12_RAYTRACING_INSTANCE_DESC instanceData =
                    GetD3D12AccelerationInstance(instance);
                mInstances.push_back(instanceData);
            }
        }

        // container requires instance buffer
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
            uint64_t bufferSize =
                descriptor->instanceCount * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);

            BufferDescriptor descriptor = {nullptr, nullptr, wgpu::BufferUsage::CopyDst,
                                           bufferSize};
            Buffer* buffer = ToBackend(device->CreateBuffer(&descriptor));
            mInstanceMemory.allocation = AcquireRef(buffer);
            mInstanceMemory.buffer = buffer->GetD3D12Resource();
            mInstanceMemory.address = mInstanceMemory.buffer.Get()->GetGPUVirtualAddress();

            // copy instance data into instance buffer
            buffer->SetSubData(0, bufferSize, mInstances.data());
        }

        // reserve scratch memory
        {
            mBuildInformation.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            mBuildInformation.Flags =
                ToD3D12RayTracingAccelerationStructureBuildFlags(descriptor->usage);
            mBuildInformation.Type = ToD3D12RayTracingAccelerationContainerLevel(descriptor->level);
            if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Bottom) {
                mBuildInformation.NumDescs = mGeometries.size();
                mBuildInformation.pGeometryDescs = mGeometries.data();
            } else if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
                mBuildInformation.NumDescs = mInstances.size();
                mBuildInformation.InstanceDescs = mInstanceMemory.buffer->GetGPUVirtualAddress();
            }
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
            device->GetD3D12Device5()->GetRaytracingAccelerationStructurePrebuildInfo(
                &mBuildInformation, &prebuildInfo);

            // align memory
            uint64_t resultMemorySize =
                Align(prebuildInfo.ResultDataMaxSizeInBytes,
                      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
            uint64_t buildMemorySize =
                Align(prebuildInfo.ScratchDataSizeInBytes,
                      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
            uint64_t updateMemorySize =
                Align(prebuildInfo.UpdateScratchDataSizeInBytes,
                      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);

            // allocate memory
            DAWN_TRY(AllocateScratchMemory(mScratchMemory.result, resultMemorySize,
                                           D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE));
            DAWN_TRY(AllocateScratchMemory(mScratchMemory.build, buildMemorySize,
                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
            if (prebuildInfo.UpdateScratchDataSizeInBytes > 0) {
                DAWN_TRY(AllocateScratchMemory(mScratchMemory.update, updateMemorySize,
                                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
            }
        }

        return {};
    }

    MaybeError RayTracingAccelerationContainer::AllocateScratchMemory(
        MemoryEntry& memoryEntry,
        uint64_t size,
        D3D12_RESOURCE_STATES initialUsage) {
        Device* device = ToBackend(GetDevice());

        D3D12_RESOURCE_DESC resourceDesc;
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Alignment = 0;
        resourceDesc.Width = size;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.SampleDesc.Quality = 0;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        DAWN_TRY_ASSIGN(memoryEntry.resource, device->AllocateMemory(D3D12_HEAP_TYPE_DEFAULT,
                                                                     resourceDesc, initialUsage));

        memoryEntry.buffer = memoryEntry.resource.GetD3D12Resource();
        memoryEntry.address = memoryEntry.buffer.Get()->GetGPUVirtualAddress();

        return {};
    }

    ScratchMemoryPool& RayTracingAccelerationContainer::GetScratchMemory() {
        return mScratchMemory;
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS&
    RayTracingAccelerationContainer::GetBuildInformation() {
        return mBuildInformation;
    }

    RayTracingAccelerationContainer::~RayTracingAccelerationContainer() {
        DestroyInternal();
    }

    void RayTracingAccelerationContainer::DestroyScratchBuildMemory() {
        Device* device = ToBackend(GetDevice());
        // delete scratch build memory
        if (mScratchMemory.build.buffer != nullptr) {
            device->DeallocateMemory(mScratchMemory.build.resource);
            mScratchMemory.build.buffer = nullptr;
        }
    }

    MaybeError RayTracingAccelerationContainer::UpdateInstanceImpl(
        uint32_t instanceIndex,
        const RayTracingAccelerationInstanceDescriptor* descriptor) {
        uint32_t start = instanceIndex * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
        uint32_t count = sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
        D3D12_RAYTRACING_INSTANCE_DESC instanceData = GetD3D12AccelerationInstance(*descriptor);
        mInstanceMemory.allocation.Get()->SetSubData(start, count,
                                                     reinterpret_cast<void*>(&instanceData));
        return {};
    }

}}  // namespace dawn_native::d3d12
