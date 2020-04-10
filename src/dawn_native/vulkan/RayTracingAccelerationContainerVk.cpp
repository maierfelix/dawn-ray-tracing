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

#include "common/Math.h"
#include "dawn_native/vulkan/DeviceVk.h"
#include "dawn_native/vulkan/FencedDeleter.h"
#include "dawn_native/vulkan/ResourceHeapVk.h"
#include "dawn_native/vulkan/UtilsVulkan.h"
#include "dawn_native/vulkan/VulkanError.h"

namespace dawn_native { namespace vulkan {

    namespace {

        VkAccelerationInstance GetVkAccelerationInstance(
            const RayTracingAccelerationInstanceDescriptor& descriptor) {
            RayTracingAccelerationContainer* geometryContainer =
                ToBackend(descriptor.geometryContainer);
            VkAccelerationInstance out;
            // process transform object
            if (descriptor.transform != nullptr) {
                float transform[16] = {};
                const Transform3D* tr = descriptor.transform->translation;
                const Transform3D* ro = descriptor.transform->rotation;
                const Transform3D* sc = descriptor.transform->scale;
                Fill4x3TransformMatrix(transform, tr->x, tr->y, tr->z, ro->x, ro->y, ro->z, sc->x,
                                       sc->y, sc->z);
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
        if (mScratchMemory.result.buffer != VK_NULL_HANDLE) {
            device->DeallocateMemory(&mScratchMemory.result.resource);
            device->GetFencedDeleter()->DeleteWhenUnused(mScratchMemory.result.buffer);
            mScratchMemory.result.buffer = VK_NULL_HANDLE;
        }
        if (mScratchMemory.update.buffer != VK_NULL_HANDLE) {
            device->DeallocateMemory(&mScratchMemory.update.resource);
            device->GetFencedDeleter()->DeleteWhenUnused(mScratchMemory.update.buffer);
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

    MaybeError RayTracingAccelerationContainer::Initialize(
        const RayTracingAccelerationContainerDescriptor* descriptor) {
        Device* device = ToBackend(GetDevice());

        // TODO: make this an extension
        if (device->fn.CreateAccelerationStructureNV == nullptr) {
            return DAWN_VALIDATION_ERROR("Invalid Call to CreateAccelerationStructureNV");
        }

        // acceleration container holds geometry
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Bottom) {
            mGeometries.reserve(descriptor->geometryCount);
            for (unsigned int ii = 0; ii < descriptor->geometryCount; ++ii) {
                const RayTracingAccelerationGeometryDescriptor& geometry =
                    descriptor->geometries[ii];

                VkGeometryNV geometryInfo;
                geometryInfo.sType = VK_STRUCTURE_TYPE_GEOMETRY_NV;
                geometryInfo.pNext = nullptr;
                geometryInfo.flags = ToVulkanAccelerationContainerGeometryFlags(geometry.flags);
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
                        ToVulkanAccelerationContainerVertexFormat(geometry.vertex->format);
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
                        ToVulkanAccelerationContainerIndexFormat(geometry.index->format);
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

        // acceleration container holds instances
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
            mInstances.reserve(descriptor->instanceCount);
            // create data for instance buffer
            for (unsigned int ii = 0; ii < descriptor->instanceCount; ++ii) {
                const RayTracingAccelerationInstanceDescriptor& instance =
                    descriptor->instances[ii];
                VkAccelerationInstance instanceData = GetVkAccelerationInstance(instance);
                mInstances.push_back(instanceData);
            };
        }

        // container requires instance buffer
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
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

        // create the acceleration container
        {
            MaybeError result = CreateAccelerationStructure(descriptor);
            if (result.IsError())
                return result.AcquireError();
        }

        // reserve scratch memory
        {
            VkMemoryRequirements resultRequirements =
                GetMemoryRequirements(VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV);

            VkMemoryRequirements buildRequirements = GetMemoryRequirements(
                VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV);

            VkMemoryRequirements updateRequirements = GetMemoryRequirements(
                VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_UPDATE_SCRATCH_NV);

            DAWN_TRY(AllocateScratchMemory(mScratchMemory.result, resultRequirements));
            DAWN_TRY(AllocateScratchMemory(mScratchMemory.build, buildRequirements));
            // update memory is optional
            if (updateRequirements.size > 0) {
                DAWN_TRY(AllocateScratchMemory(mScratchMemory.update, updateRequirements));
            }
        }

        // bind scratch result memory
        {
            VkBindAccelerationStructureMemoryInfoNV memoryBindInfo;
            memoryBindInfo.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
            memoryBindInfo.pNext = nullptr;
            memoryBindInfo.accelerationStructure = GetAccelerationStructure();
            memoryBindInfo.memory = mScratchMemory.result.memory;
            memoryBindInfo.memoryOffset = mScratchMemory.result.offset;
            memoryBindInfo.deviceIndexCount = 0;
            memoryBindInfo.pDeviceIndices = nullptr;

            // make sure the memory got allocated properly
            if (memoryBindInfo.memory == VK_NULL_HANDLE) {
                return DAWN_VALIDATION_ERROR("Failed to allocate Scratch Memory");
            }

            DAWN_TRY(CheckVkSuccess(device->fn.BindAccelerationStructureMemoryNV(
                                        device->GetVkDevice(), 1, &memoryBindInfo),
                                    "vkBindAccelerationStructureMemoryNV"));
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
        Device* device = ToBackend(GetDevice());
        // delete scratch build memory
        if (mScratchMemory.build.buffer != VK_NULL_HANDLE) {
            device->DeallocateMemory(&mScratchMemory.build.resource);
            device->GetFencedDeleter()->DeleteWhenUnused(mScratchMemory.build.buffer);
            mScratchMemory.build.buffer = VK_NULL_HANDLE;
        }
    }

    MaybeError RayTracingAccelerationContainer::AllocateScratchMemory(
        MemoryEntry& memoryEntry,
        VkMemoryRequirements& requirements) {
        Device* device = ToBackend(GetDevice());

        VkBufferCreateInfo createInfo;
        createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        createInfo.pNext = nullptr;
        createInfo.flags = 0;
        createInfo.size = requirements.size;
        createInfo.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
        createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = 0;

        DAWN_TRY(CheckVkSuccess(device->fn.CreateBuffer(device->GetVkDevice(), &createInfo, nullptr,
                                                        &*(memoryEntry.buffer)),
                                "vkCreateBuffer"));

        VkMemoryRequirements2 bufferSizeRequirements;
        bufferSizeRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
        bufferSizeRequirements.pNext = nullptr;

        VkBufferMemoryRequirementsInfo2 bufferMemoryRequirements;
        bufferMemoryRequirements.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2;
        bufferMemoryRequirements.pNext = nullptr;
        bufferMemoryRequirements.buffer = memoryEntry.buffer;

        device->fn.GetBufferMemoryRequirements2(device->GetVkDevice(), &bufferMemoryRequirements,
                                                &bufferSizeRequirements);

        requirements.size = bufferSizeRequirements.memoryRequirements.size;
        // TODO: validate that this is legal - validation layers seem to accept this?
        requirements.memoryTypeBits = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        DAWN_TRY_ASSIGN(memoryEntry.resource, device->AllocateMemory(requirements, false));

        DAWN_TRY(CheckVkSuccess(device->fn.BindBufferMemory(
                                    device->GetVkDevice(), memoryEntry.buffer,
                                    ToBackend(memoryEntry.resource.GetResourceHeap())->GetMemory(),
                                    memoryEntry.resource.GetOffset()),
                                "vkBindBufferMemory"));

        memoryEntry.memory = ToBackend(memoryEntry.resource.GetResourceHeap())->GetMemory();
        memoryEntry.offset = memoryEntry.resource.GetOffset();

        return {};
    }

    VkMemoryRequirements RayTracingAccelerationContainer::GetMemoryRequirements(
        VkAccelerationStructureMemoryRequirementsTypeNV type) const {
        Device* device = ToBackend(GetDevice());

        VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo;
        memoryRequirementsInfo.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
        memoryRequirementsInfo.pNext = nullptr;
        memoryRequirementsInfo.accelerationStructure = mAccelerationStructure;
        memoryRequirementsInfo.type = type;

        VkMemoryRequirements2 memoryRequirements2;
        memoryRequirements2.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
        memoryRequirements2.pNext = nullptr;

        device->fn.GetAccelerationStructureMemoryRequirementsNV(
            device->GetVkDevice(), &memoryRequirementsInfo, &memoryRequirements2);

        return memoryRequirements2.memoryRequirements;
    }

    uint64_t RayTracingAccelerationContainer::GetMemoryRequirementSize(
        VkAccelerationStructureMemoryRequirementsTypeNV type) const {
        return GetMemoryRequirements(type).size;
    }

    MaybeError RayTracingAccelerationContainer::CreateAccelerationStructure(
        const RayTracingAccelerationContainerDescriptor* descriptor) {
        Device* device = ToBackend(GetDevice());

        VkAccelerationStructureCreateInfoNV accelerationStructureInfo;
        accelerationStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
        accelerationStructureInfo.pNext = nullptr;
        accelerationStructureInfo.compactedSize = 0;

        accelerationStructureInfo.info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
        accelerationStructureInfo.info.pNext = nullptr;
        accelerationStructureInfo.info.flags = 0;
        accelerationStructureInfo.info.flags =
            ToVulkanBuildAccelerationContainerFlags(descriptor->flags);
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
            accelerationStructureInfo.info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
            accelerationStructureInfo.info.instanceCount = mInstanceCount;
            accelerationStructureInfo.info.geometryCount = 0;
            accelerationStructureInfo.info.pGeometries = nullptr;
        } else if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Bottom) {
            accelerationStructureInfo.info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
            accelerationStructureInfo.info.instanceCount = 0;
            accelerationStructureInfo.info.geometryCount = descriptor->geometryCount;
            accelerationStructureInfo.info.pGeometries = mGeometries.data();
        } else {
            return DAWN_VALIDATION_ERROR("Invalid Acceleration Container Level");
        }

        MaybeError result = CheckVkSuccess(device->fn.CreateAccelerationStructureNV(
                                               device->GetVkDevice(), &accelerationStructureInfo,
                                               nullptr, &*mAccelerationStructure),
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