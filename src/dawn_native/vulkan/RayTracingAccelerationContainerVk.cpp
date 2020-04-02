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

#include "dawn_native/vulkan/RayTracingAccelerationContainerVk.h"

#include "dawn_native/vulkan/DeviceVk.h"
#include "dawn_native/vulkan/FencedDeleter.h"
#include "dawn_native/vulkan/ResourceHeapVk.h"
#include "dawn_native/vulkan/UtilsVulkan.h"
#include "dawn_native/vulkan/VulkanError.h"

namespace dawn_native { namespace vulkan {

    namespace {

        // generates a 4x3 transform matrix in row-major order
        void Fill4x3TransformMatrix(float* out,
                                    const Transform3D* translation,
                                    const Transform3D* rotation,
                                    const Transform3D* scale) {
            const float PI = 3.14159265358979f;

            // make identity
            out[0] = 1.0f;
            out[5] = 1.0f;
            out[10] = 1.0f;
            out[15] = 1.0f;
            // apply translation
            if (translation != nullptr) {
                float x = translation->x;
                float y = translation->y;
                float z = translation->z;
                out[12] = out[0] * x + out[4] * y + out[8] * z + out[12];
                out[13] = out[1] * x + out[5] * y + out[9] * z + out[13];
                out[14] = out[2] * x + out[6] * y + out[10] * z + out[14];
                out[15] = out[3] * x + out[7] * y + out[11] * z + out[15];
            }
            // apply rotation
            if (rotation != nullptr) {
                // TODO: beautify this
                float x = rotation->x;
                float y = rotation->y;
                float z = rotation->z;
                // x rotation
                if (x != 0.0f) {
                    x = x * (PI / 180.0f);
                    float s = sinf(x);
                    float c = cosf(x);
                    float a10 = out[4];
                    float a11 = out[5];
                    float a12 = out[6];
                    float a13 = out[7];
                    float a20 = out[8];
                    float a21 = out[9];
                    float a22 = out[10];
                    float a23 = out[11];
                    out[4] = a10 * c + a20 * s;
                    out[5] = a11 * c + a21 * s;
                    out[6] = a12 * c + a22 * s;
                    out[7] = a13 * c + a23 * s;
                    out[8] = a20 * c - a10 * s;
                    out[9] = a21 * c - a11 * s;
                    out[10] = a22 * c - a12 * s;
                    out[11] = a23 * c - a13 * s;
                }
                // y rotation
                if (y != 0.0f) {
                    y = y * (PI / 180.0f);
                    float s = sinf(y);
                    float c = cosf(y);
                    float a00 = out[0];
                    float a01 = out[1];
                    float a02 = out[2];
                    float a03 = out[3];
                    float a20 = out[8];
                    float a21 = out[9];
                    float a22 = out[10];
                    float a23 = out[11];
                    out[0] = a00 * c - a20 * s;
                    out[1] = a01 * c - a21 * s;
                    out[2] = a02 * c - a22 * s;
                    out[3] = a03 * c - a23 * s;
                    out[8] = a00 * s + a20 * c;
                    out[9] = a01 * s + a21 * c;
                    out[10] = a02 * s + a22 * c;
                    out[11] = a03 * s + a23 * c;
                }
                // z rotation
                if (z != 0.0f) {
                    z = z * (PI / 180.0f);
                    float s = sinf(z);
                    float c = cosf(z);
                    float a00 = out[0];
                    float a01 = out[1];
                    float a02 = out[2];
                    float a03 = out[3];
                    float a10 = out[4];
                    float a11 = out[5];
                    float a12 = out[6];
                    float a13 = out[7];
                    out[0] = a00 * c + a10 * s;
                    out[1] = a01 * c + a11 * s;
                    out[2] = a02 * c + a12 * s;
                    out[3] = a03 * c + a13 * s;
                    out[4] = a10 * c - a00 * s;
                    out[5] = a11 * c - a01 * s;
                    out[6] = a12 * c - a02 * s;
                    out[7] = a13 * c - a03 * s;
                }
            }
            // apply scale
            if (scale != nullptr) {
                float x = scale->x;
                float y = scale->y;
                float z = scale->z;
                out[0] = out[0] * x;
                out[1] = out[1] * x;
                out[2] = out[2] * x;
                out[3] = out[3] * x;
                out[4] = out[4] * y;
                out[5] = out[5] * y;
                out[6] = out[6] * y;
                out[7] = out[7] * y;
                out[8] = out[8] * z;
                out[9] = out[9] * z;
                out[10] = out[10] * z;
                out[11] = out[11] * z;
            }
            // turn into 4x3
            out[3] = out[12];
            out[7] = out[13];
            out[11] = out[14];
            // reset last row
            out[12] = 0.0f;
            out[13] = 0.0f;
            out[14] = 0.0f;
            out[15] = 0.0f;
        }

        VkAccelerationInstance GetVkAccelerationInstance(
            const RayTracingAccelerationInstanceDescriptor& descriptor) {
            RayTracingAccelerationContainer* geometryContainer =
                ToBackend(descriptor.geometryContainer);
            VkAccelerationInstance out{};
            // process transform object
            if (descriptor.transform != nullptr) {
                float transform[16] = {};
                Fill4x3TransformMatrix(transform, descriptor.transform->translation,
                                       descriptor.transform->rotation, descriptor.transform->scale);
                memcpy(&out.transform, transform, sizeof(out.transform));
            }
            // process transform matrix
            else if (descriptor.transformMatrix != nullptr) {
                memcpy(&out.transform, descriptor.transformMatrix, sizeof(out.transform));
            }
            out.instanceId = descriptor.instanceId;
            out.mask = descriptor.mask;
            out.instanceOffset = descriptor.instanceOffset;
            out.flags = ToVulkanAccelerationContainerInstanceFlags(descriptor.flags);
            out.accelerationStructureHandle = geometryContainer->GetHandle();
            return out;
        }

    }  // anonymous namespace

    // validate geometry instance flag bits to match with wgpu
    // we have to do this since we allow instance buffer to be created from outside
    static_assert((VkGeometryInstanceFlagBitsNV)
                          wgpu::RayTracingAccelerationInstanceFlag::TriangleCullDisable ==
                      VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV,
                  "");
    static_assert((VkGeometryInstanceFlagBitsNV)
                          wgpu::RayTracingAccelerationInstanceFlag::TriangleFrontCounterclockwise ==
                      VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_NV,
                  "");
    static_assert((VkGeometryInstanceFlagBitsNV)
                          wgpu::RayTracingAccelerationInstanceFlag::ForceOpaque ==
                      VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_NV,
                  "");
    static_assert((VkGeometryInstanceFlagBitsNV)
                          wgpu::RayTracingAccelerationInstanceFlag::ForceNoOpaque ==
                      VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_NV,
                  "");

    // static
    ResultOrError<RayTracingAccelerationContainer*> RayTracingAccelerationContainer::Create(
        Device* device,
        const RayTracingAccelerationContainerDescriptor* descriptor) {
        std::unique_ptr<RayTracingAccelerationContainer> geometry =
            std::make_unique<RayTracingAccelerationContainer>(device, descriptor);
        DAWN_TRY(geometry->Initialize(descriptor));
        return geometry.release();
    }

    void RayTracingAccelerationContainer::DestroyImpl() {
        Device* device = ToBackend(GetDevice());
        DestroyScratchBuildMemory();
        if (mScratchMemory.result.buffer != VK_NULL_HANDLE) {
            Buffer* buffer = mScratchMemory.result.allocation.Get();
            buffer->Destroy();
            mScratchMemory.result.buffer = VK_NULL_HANDLE;
        }
        if (mScratchMemory.update.buffer != VK_NULL_HANDLE) {
            Buffer* buffer = mScratchMemory.update.allocation.Get();
            buffer->Destroy();
            mScratchMemory.update.buffer = VK_NULL_HANDLE;
        }
        if (mInstanceMemory.buffer != VK_NULL_HANDLE) {
            Buffer* buffer = mInstanceMemory.allocation.Get();
            if (buffer != nullptr) {
                buffer->Destroy();
            }
            mInstanceMemory.buffer = VK_NULL_HANDLE;
        }
        if (mAccelerationStructure != VK_NULL_HANDLE) {
            // delete acceleration structure
            device->GetFencedDeleter()->DeleteWhenUnused(mAccelerationStructure);
            mAccelerationStructure = VK_NULL_HANDLE;
        }
    }

    uint64_t RayTracingAccelerationContainer::GetHandleImpl() {
        return mHandle;
    }

    MaybeError RayTracingAccelerationContainer::Initialize(
        const RayTracingAccelerationContainerDescriptor* descriptor) {
        Device* device = ToBackend(GetDevice());

        // validate ray tracing calls
        if (device->fn.CreateAccelerationStructureNV == nullptr) {
            return DAWN_VALIDATION_ERROR("Invalid Call to CreateAccelerationStructureNV");
        }

        // acceleration container holds geometry
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Bottom) {
            mGeometries.reserve(descriptor->geometryCount);
            for (unsigned int ii = 0; ii < descriptor->geometryCount; ++ii) {
                const RayTracingAccelerationGeometryDescriptor& geometry =
                    descriptor->geometries[ii];

                VkGeometryNV geometryInfo{};
                geometryInfo.pNext = nullptr;
                geometryInfo.sType = VK_STRUCTURE_TYPE_GEOMETRY_NV;
                geometryInfo.flags = ToVulkanAccelerationContainerGeometryFlags(geometry.flags);
                geometryInfo.geometryType = ToVulkanGeometryType(geometry.type);
                // reset
                geometryInfo.geometry.triangles.sType = VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV;
                geometryInfo.geometry.triangles.pNext = nullptr;
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
                    geometryInfo.geometry.triangles.vertexStride = 0;
                    geometryInfo.geometry.triangles.vertexCount = 0;
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
                // no transform data support (yet?)
                geometryInfo.geometry.triangles.transformData = nullptr;
                geometryInfo.geometry.triangles.transformOffset = 0;
                mGeometries.push_back(geometryInfo);
            };
        }

        // acceleration container holds instances
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
            // only use instances array when no instance buffer was provided
            if (descriptor->instanceBuffer == nullptr) {
                // create data for instance buffer
                mInstances.reserve(descriptor->instanceCount);
                for (unsigned int ii = 0; ii < descriptor->instanceCount; ++ii) {
                    const RayTracingAccelerationInstanceDescriptor& instance =
                        descriptor->instances[ii];
                    VkAccelerationInstance instanceData = GetVkAccelerationInstance(instance);
                    mInstances.push_back(instanceData);
                };
            }
        }

        // container requires instance buffer
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
            // only create internal instance buffer when no external one was provided
            if (descriptor->instanceBuffer == nullptr) {
                uint64_t bufferSize = descriptor->instanceCount * sizeof(VkAccelerationInstance);

                BufferDescriptor descriptor = {nullptr, nullptr, wgpu::BufferUsage::CopyDst,
                                               bufferSize};
                Buffer* buffer = ToBackend(device->CreateBuffer(&descriptor));
                mInstanceMemory.allocation = AcquireRef(buffer);
                mInstanceMemory.buffer = buffer->GetHandle();
                mInstanceMemory.offset = buffer->GetMemoryResource().GetOffset();
                mInstanceMemory.memory =
                    ToBackend(buffer->GetMemoryResource().GetResourceHeap())->GetMemory();

                // copy instance data into instance buffer
                buffer->SetSubData(0, bufferSize, mInstances.data());
                mInstanceCount = mInstances.size();
            }
            // external instance buffer
            else {
                Buffer* buffer = ToBackend(descriptor->instanceBuffer);
                mInstanceMemory.buffer = buffer->GetHandle();
                mInstanceMemory.offset = buffer->GetMemoryResource().GetOffset();
                mInstanceMemory.memory =
                    ToBackend(buffer->GetMemoryResource().GetResourceHeap())->GetMemory();
                mInstanceCount = buffer->GetSize() / sizeof(VkAccelerationInstance);
            }
        }

        // create the acceleration container
        {
            MaybeError result = CreateAccelerationStructure(descriptor);
            if (result.IsError())
                return result.AcquireError();
        }

        // reserve scratch memory
        {
            MaybeError result = ReserveScratchMemory(descriptor);
            if (result.IsError())
                return result.AcquireError();
        }

        // take handle
        {
            uint64_t handle = 0;
            MaybeError result = FetchHandle(&handle);
            if (result.IsError())
                return result.AcquireError();
            mHandle = handle;
        }

        return {};
    }

    RayTracingAccelerationContainer::~RayTracingAccelerationContainer() {
        DestroyInternal();
    }

    void RayTracingAccelerationContainer::DestroyScratchBuildMemory() {
        // delete scratch build memory
        if (mScratchMemory.build.buffer != VK_NULL_HANDLE) {
            Buffer* buffer = mScratchMemory.build.allocation.Get();
            buffer->Destroy();
            mScratchMemory.build.buffer = VK_NULL_HANDLE;
        }
    }

    MaybeError RayTracingAccelerationContainer::ReserveScratchMemory(
        const RayTracingAccelerationContainerDescriptor* descriptor) {
        Device* device = ToBackend(GetDevice());

        // create scratch memory for this container
        uint64_t resultSize =
            GetMemoryRequirementSize(VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV);
        uint64_t buildSize = GetMemoryRequirementSize(
            VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV);
        uint64_t updateSize = GetMemoryRequirementSize(
            VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_UPDATE_SCRATCH_NV);

        // allocate scratch result memory
        {
            BufferDescriptor descriptor = {nullptr, nullptr, wgpu::BufferUsage::CopyDst,
                                           resultSize};
            Buffer* buffer = ToBackend(device->CreateBuffer(&descriptor));
            mScratchMemory.result.allocation = AcquireRef(buffer);
            mScratchMemory.result.buffer = buffer->GetHandle();
            mScratchMemory.result.offset = buffer->GetMemoryResource().GetOffset();
            mScratchMemory.result.memory =
                ToBackend(buffer->GetMemoryResource().GetResourceHeap())->GetMemory();
        }

        // allocate scratch build memory
        {
            BufferDescriptor descriptor = {nullptr, nullptr, wgpu::BufferUsage::CopyDst, buildSize};
            Buffer* buffer = ToBackend(device->CreateBuffer(&descriptor));
            mScratchMemory.build.allocation = AcquireRef(buffer);
            mScratchMemory.build.buffer = buffer->GetHandle();
            mScratchMemory.build.offset = buffer->GetMemoryResource().GetOffset();
            mScratchMemory.build.memory =
                ToBackend(buffer->GetMemoryResource().GetResourceHeap())->GetMemory();
        }

        // allocate scratch update memory
        if (updateSize > 0) {
            BufferDescriptor descriptor = {nullptr, nullptr, wgpu::BufferUsage::CopyDst,
                                           updateSize};
            Buffer* buffer = ToBackend(device->CreateBuffer(&descriptor));
            mScratchMemory.update.allocation = AcquireRef(buffer);
            mScratchMemory.update.buffer = buffer->GetHandle();
            mScratchMemory.update.offset = buffer->GetMemoryResource().GetOffset();
            mScratchMemory.update.memory =
                ToBackend(buffer->GetMemoryResource().GetResourceHeap())->GetMemory();
        }

        // bind scratch result memory
        VkBindAccelerationStructureMemoryInfoNV memoryBindInfo{};
        memoryBindInfo.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
        memoryBindInfo.accelerationStructure = GetAccelerationStructure();
        memoryBindInfo.memory = mScratchMemory.result.memory;
        memoryBindInfo.memoryOffset = mScratchMemory.result.offset;
        memoryBindInfo.deviceIndexCount = 0;
        memoryBindInfo.pDeviceIndices = nullptr;

        // make sure the memory got allocated properly
        if (memoryBindInfo.memory == VK_NULL_HANDLE) {
            return DAWN_VALIDATION_ERROR("Failed to allocate Scratch Memory");
        }

        DAWN_TRY(CheckVkSuccess(
            device->fn.BindAccelerationStructureMemoryNV(device->GetVkDevice(), 1, &memoryBindInfo),
            "vkBindAccelerationStructureMemoryNV"));

        return {};
    }

    VkMemoryRequirements2 RayTracingAccelerationContainer::GetMemoryRequirements(
        VkAccelerationStructureMemoryRequirementsTypeNV type) const {
        Device* device = ToBackend(GetDevice());

        VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo{};
        memoryRequirementsInfo.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
        memoryRequirementsInfo.pNext = nullptr;
        memoryRequirementsInfo.accelerationStructure = mAccelerationStructure;

        VkMemoryRequirements2 memoryRequirements2{};
        device->fn.GetAccelerationStructureMemoryRequirementsNV(
            device->GetVkDevice(), &memoryRequirementsInfo, &memoryRequirements2);

        return memoryRequirements2;
    }

    uint64_t RayTracingAccelerationContainer::GetMemoryRequirementSize(
        VkAccelerationStructureMemoryRequirementsTypeNV type) const {
        return GetMemoryRequirements(type).memoryRequirements.size;
    }

    MaybeError RayTracingAccelerationContainer::CreateAccelerationStructure(
        const RayTracingAccelerationContainerDescriptor* descriptor) {
        Device* device = ToBackend(GetDevice());

        VkAccelerationStructureCreateInfoNV accelerationStructureCI{};
        accelerationStructureCI.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
        accelerationStructureCI.compactedSize = 0;

        accelerationStructureCI.info = {};
        accelerationStructureCI.info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
        accelerationStructureCI.info.flags =
            ToVulkanBuildAccelerationContainerFlags(descriptor->flags);
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
            accelerationStructureCI.info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
            accelerationStructureCI.info.instanceCount = mInstanceCount;
            accelerationStructureCI.info.geometryCount = 0;
            accelerationStructureCI.info.pGeometries = nullptr;
        } else if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Bottom) {
            accelerationStructureCI.info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
            accelerationStructureCI.info.instanceCount = 0;
            accelerationStructureCI.info.geometryCount = descriptor->geometryCount;
            accelerationStructureCI.info.pGeometries = mGeometries.data();
        } else {
            return DAWN_VALIDATION_ERROR("Invalid Acceleration Container Level");
        }

        MaybeError result = CheckVkSuccess(
            device->fn.CreateAccelerationStructureNV(
                device->GetVkDevice(), &accelerationStructureCI, nullptr, &*mAccelerationStructure),
            "vkCreateAccelerationStructureNV");
        if (result.IsError())
            return result;

        return {};
    }

    MemoryEntry& RayTracingAccelerationContainer::GetInstanceMemory() {
        return mInstanceMemory;
    }

    MaybeError RayTracingAccelerationContainer::FetchHandle(uint64_t* handle) {
        Device* device = ToBackend(GetDevice());
        MaybeError result = CheckVkSuccess(
            device->fn.GetAccelerationStructureHandleNV(
                device->GetVkDevice(), mAccelerationStructure, sizeof(uint64_t), handle),
            "vkGetAccelerationStructureHandleNV");
        if (result.IsError())
            return result;
        return {};
    }

    MaybeError RayTracingAccelerationContainer::UpdateInstanceImpl(
        uint32_t instanceIndex,
        const RayTracingAccelerationInstanceDescriptor* descriptor) {
        uint32_t start = instanceIndex * sizeof(VkAccelerationInstance);
        uint32_t count = sizeof(VkAccelerationInstance);
        VkAccelerationInstance instanceData = GetVkAccelerationInstance(*descriptor);

        mInstanceMemory.allocation.Get()->SetSubData(start, count,
                                                     reinterpret_cast<void*>(&instanceData));

        return {};
    }

    uint64_t RayTracingAccelerationContainer::GetHandle() const {
        return mHandle;
    }

    VkAccelerationStructureNV RayTracingAccelerationContainer::GetAccelerationStructure() const {
        return mAccelerationStructure;
    }

    std::vector<VkGeometryNV>& RayTracingAccelerationContainer::GetGeometries() {
        return mGeometries;
    }

    uint32_t RayTracingAccelerationContainer::GetInstanceCount() const {
        return mInstanceCount;
    }

    ScratchMemoryPool& RayTracingAccelerationContainer::GetScratchMemory() {
        return mScratchMemory;
    }

}}  // namespace dawn_native::vulkan