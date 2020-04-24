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

        VkAccelerationStructureInstanceKHR GetVkAccelerationInstance(
            const RayTracingAccelerationInstanceDescriptor& descriptor) {
            RayTracingAccelerationContainer* geometryContainer =
                ToBackend(descriptor.geometryContainer);
            VkAccelerationStructureInstanceKHR out;
            // process transform object
            if (descriptor.transform != nullptr) {
                float transform[16] = {};
                const Transform3D* tr = descriptor.transform->translation;
                const Transform3D* ro = descriptor.transform->rotation;
                const Transform3D* sc = descriptor.transform->scale;
                Fill4x3TransformMatrix(transform, tr->x, tr->y, tr->z, ro->x, ro->y, ro->z, sc->x,
                                       sc->y, sc->z);
                memcpy(&out.transform.matrix, transform, sizeof(out.transform));
            }
            // process transform matrix
            else if (descriptor.transformMatrix != nullptr) {
                memcpy(&out.transform.matrix, descriptor.transformMatrix, sizeof(out.transform));
            }
            out.instanceCustomIndex = descriptor.instanceId;
            out.mask = descriptor.mask;
            out.instanceShaderBindingTableRecordOffset = descriptor.instanceOffset;
            out.flags = ToVulkanAccelerationContainerInstanceFlags(descriptor.flags);
            out.accelerationStructureReference = geometryContainer->GetHandle();
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

        // acceleration container holds geometry
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Bottom) {
            mGeometries.reserve(descriptor->geometryCount);
            for (unsigned int ii = 0; ii < descriptor->geometryCount; ++ii) {
                const RayTracingAccelerationGeometryDescriptor& geometry =
                    descriptor->geometries[ii];

                VkAccelerationStructureGeometryKHR geometryInfo;
                geometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
                geometryInfo.pNext = nullptr;
                geometryInfo.flags = ToVulkanAccelerationContainerGeometryFlags(geometry.flags);
                geometryInfo.geometryType = ToVulkanGeometryType(geometry.type);

                // vertex buffer
                if (geometry.vertex != nullptr && geometry.vertex->buffer != nullptr) {
                    Buffer* vertexBuffer = ToBackend(geometry.vertex->buffer);
                    geometryInfo.geometry.triangles.sType =
                        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                    geometryInfo.geometry.triangles.pNext = nullptr;
                    geometryInfo.geometry.triangles.vertexFormat =
                        ToVulkanAccelerationContainerVertexFormat(geometry.vertex->format);
                    geometryInfo.geometry.triangles.vertexStride = geometry.vertex->stride;
                    geometryInfo.geometry.triangles.vertexData.deviceAddress =
                        vertexBuffer->GetDeviceAddress() + geometry.vertex->offset;
                    // index buffer
                    if (geometry.index != nullptr && geometry.index->buffer != nullptr) {
                        Buffer* indexBuffer = ToBackend(geometry.index->buffer);
                        geometryInfo.geometry.triangles.indexType =
                            ToVulkanAccelerationContainerIndexFormat(geometry.index->format);
                        geometryInfo.geometry.triangles.indexData.deviceAddress =
                            indexBuffer->GetDeviceAddress() + geometry.index->offset;
                    } else {
                        geometryInfo.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
                    }
                }
                // aabb buffer
                if (geometry.aabb != nullptr && geometry.aabb->buffer != nullptr) {
                    Buffer* aabbBuffer = ToBackend(geometry.aabb->buffer);
                    geometryInfo.geometry.aabbs.sType =
                        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
                    geometryInfo.geometry.aabbs.pNext = nullptr;
                    geometryInfo.geometry.aabbs.stride = geometry.aabb->stride;
                    geometryInfo.geometry.aabbs.data.deviceAddress =
                        aabbBuffer->GetDeviceAddress() + geometry.aabb->offset;
                }
                mGeometries.push_back(geometryInfo);
            };
        }

        // acceleration container holds instances
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
            mGeometries.reserve(1);

            uint64_t bufferSize =
                descriptor->instanceCount * sizeof(VkAccelerationStructureInstanceKHR);

            // create internal buffer holding instances
            BufferDescriptor bufDescriptor = {nullptr, nullptr, wgpu::BufferUsage::CopyDst,
                                           bufferSize};
            Buffer* buffer = ToBackend(device->CreateBuffer(&bufDescriptor));
            mInstanceMemory.allocation = AcquireRef(buffer);
            mInstanceMemory.buffer = buffer->GetHandle();
            mInstanceMemory.offset = buffer->GetMemoryResource().GetOffset();
            mInstanceMemory.memory =
                ToBackend(buffer->GetMemoryResource().GetResourceHeap())->GetMemory();

            VkAccelerationStructureGeometryKHR geometryInfo;
            geometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            geometryInfo.pNext = nullptr;
            geometryInfo.flags = 0;
            geometryInfo.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
            geometryInfo.geometry.instances.sType =
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
            geometryInfo.geometry.instances.pNext = nullptr;
            geometryInfo.geometry.instances.arrayOfPointers = VK_FALSE;
            geometryInfo.geometry.instances.data.deviceAddress = 0;  // TODO: address
            mGeometries.push_back(geometryInfo);

            // copy instance data into instance buffer
            std::vector<VkAccelerationStructureInstanceKHR> mInstances;
            mInstances.reserve(descriptor->instanceCount);
            for (unsigned int ii = 0; ii < descriptor->instanceCount; ++ii) {
                mInstances.push_back(GetVkAccelerationInstance(descriptor->instances[ii]));
            };
            buffer->SetSubData(0, bufferSize, mInstances.data());
        }

        // create the acceleration container
        {
            MaybeError result = CreateAccelerationStructure(descriptor);
            if (result.IsError())
                return result.AcquireError();
        }

        // reserve scratch memory
        {
            VkMemoryRequirements resultRequirements = GetMemoryRequirements(
                VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_KHR);

            VkMemoryRequirements buildRequirements = GetMemoryRequirements(
                VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_KHR);

            VkMemoryRequirements updateRequirements = GetMemoryRequirements(
                VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_UPDATE_SCRATCH_KHR);

            DAWN_TRY(AllocateScratchMemory(mScratchMemory.result, resultRequirements));
            DAWN_TRY(AllocateScratchMemory(mScratchMemory.build, buildRequirements));
            // update memory is optional
            if (updateRequirements.size > 0) {
                DAWN_TRY(AllocateScratchMemory(mScratchMemory.update, updateRequirements));
            }
        }

        // bind scratch result memory
        {
            VkBindAccelerationStructureMemoryInfoKHR memoryBindInfo;
            memoryBindInfo.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_KHR;
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

            DAWN_TRY(CheckVkSuccess(device->fn.BindAccelerationStructureMemoryKHR(
                                        device->GetVkDevice(), 1, &memoryBindInfo),
                                    "vkBindAccelerationStructureMemoryKHR"));
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
        createInfo.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_KHR;
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
        requirements.alignment = bufferSizeRequirements.memoryRequirements.alignment;
        requirements.memoryTypeBits =
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;  // validate that this is legal

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
        VkAccelerationStructureMemoryRequirementsTypeKHR type) const {
        Device* device = ToBackend(GetDevice());

        VkAccelerationStructureMemoryRequirementsInfoKHR memoryRequirementsInfo;
        memoryRequirementsInfo.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_KHR;
        memoryRequirementsInfo.pNext = nullptr;
        memoryRequirementsInfo.accelerationStructure = mAccelerationStructure;
        memoryRequirementsInfo.type = type;
        memoryRequirementsInfo.buildType =
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR;  // TODO: allow host building?

        VkMemoryRequirements2 memoryRequirements2;
        memoryRequirements2.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
        memoryRequirements2.pNext = nullptr;

        device->fn.GetAccelerationStructureMemoryRequirementsKHR(
            device->GetVkDevice(), &memoryRequirementsInfo, &memoryRequirements2);

        return memoryRequirements2.memoryRequirements;
    }

    uint64_t RayTracingAccelerationContainer::GetMemoryRequirementSize(
        VkAccelerationStructureMemoryRequirementsTypeKHR type) const {
        return GetMemoryRequirements(type).size;
    }

    MaybeError RayTracingAccelerationContainer::CreateAccelerationStructure(
        const RayTracingAccelerationContainerDescriptor* descriptor) {
        Device* device = ToBackend(GetDevice());

        std::vector<VkAccelerationStructureCreateGeometryTypeInfoKHR> accelerationGeometries;
        geometries.reserve(descriptor->geometryCount);
        for (unsigned int ii = 0; ii < descriptor->geometryCount; ++ii) {
            const RayTracingAccelerationGeometryDescriptor& geometry = descriptor->geometries[ii];
            VkAccelerationStructureCreateGeometryTypeInfoKHR accelerationCreateGeometryInfo;
            accelerationCreateGeometryInfo.sType =
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_GEOMETRY_TYPE_INFO_KHR;
            accelerationCreateGeometryInfo.pNext = nullptr;
            accelerationCreateGeometryInfo.geometryType = ToVulkanGeometryType(geometry.type);
            accelerationCreateGeometryInfo.allowsTransforms = VK_FALSE;
            accelerationCreateGeometryInfo.maxVertexCount = 0;
            accelerationCreateGeometryInfo.vertexFormat = VK_FORMAT_UNDEFINED;
            accelerationCreateGeometryInfo.indexType = VK_INDEX_TYPE_NONE_KHR;
            // vertex buffer
            if (geometry.vertex != nullptr && geometry.vertex->buffer != nullptr) {
                accelerationCreateGeometryInfo.maxVertexCount = geometry.vertex->count;
                accelerationCreateGeometryInfo.vertexFormat =
                    ToVulkanAccelerationContainerVertexFormat(geometry.vertex->format);
            }
            // index buffer
            if (geometry.index != nullptr && geometry.index->buffer != nullptr) {
                accelerationCreateGeometryInfo.indexType =
                    ToVulkanAccelerationContainerIndexFormat(geometry.index->format);
                accelerationCreateGeometryInfo.maxPrimitiveCount = geometry.index->count;
            }
            // aabb buffer
            if (geometry.aabb != nullptr && geometry.aabb->buffer != nullptr) {
                accelerationCreateGeometryInfo.maxPrimitiveCount = geometry.aabb->count;
            }
            accelerationGeometries.push_back(accelerationCreateGeometryInfo);
        };

        VkAccelerationStructureCreateInfoKHR accelerationStructureInfo;
        accelerationStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        accelerationStructureInfo.pNext = nullptr;
        accelerationStructureInfo.compactedSize = 0;
        accelerationStructureInfo.flags =
            ToVulkanBuildAccelerationContainerFlags(descriptor->flags);
        accelerationStructureInfo.maxGeometryCount = descriptor->geometryCount;
        accelerationStructureInfo.pGeometryInfos = accelerationGeometries.data();
        accelerationStructureInfo.type = ToVulkanAccelerationContainerLevel(descriptor->level);
        accelerationStructureInfo.deviceAddress = 0;

        MaybeError result = CheckVkSuccess(device->fn.CreateAccelerationStructureKHR(
                                               device->GetVkDevice(), &accelerationStructureInfo,
                                               nullptr, &*mAccelerationStructure),
                                           "vkCreateAccelerationStructureKHR");
        if (result.IsError())
            return result;

        return {};
    }

    MemoryEntry& RayTracingAccelerationContainer::GetInstanceMemory() {
        return mInstanceMemory;
    }

    MaybeError RayTracingAccelerationContainer::FetchHandle(uint64_t* handle) {
        Device* device = ToBackend(GetDevice());
        VkAccelerationStructureDeviceAddressInfoKHR devAddrInfo;
        devAddrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        devAddrInfo.pNext = nullptr;
        devAddrInfo.accelerationStructure = mAccelerationStructure;
        *handle = device->fn.GetAccelerationStructureDeviceAddressKHR(device->GetVkDevice(),
                                                                      &devAddrInfo);
        return {};
    }

    MaybeError RayTracingAccelerationContainer::UpdateInstanceImpl(
        uint32_t instanceIndex,
        const RayTracingAccelerationInstanceDescriptor* descriptor) {
        uint32_t start = instanceIndex * sizeof(VkAccelerationStructureInstanceKHR);
        uint32_t count = sizeof(VkAccelerationStructureInstanceKHR);
        VkAccelerationStructureInstanceKHR instanceData = GetVkAccelerationInstance(*descriptor);

        mInstanceMemory.allocation.Get()->SetSubData(start, count,
                                                     reinterpret_cast<void*>(&instanceData));

        return {};
    }

    uint64_t RayTracingAccelerationContainer::GetHandle() const {
        return mHandle;
    }

    VkAccelerationStructureKHR RayTracingAccelerationContainer::GetAccelerationStructure() const {
        return mAccelerationStructure;
    }

    std::vector<VkAccelerationStructureGeometryKHR>&
    RayTracingAccelerationContainer::GetGeometries() {
        return mGeometries;
    }

    ScratchMemoryPool& RayTracingAccelerationContainer::GetScratchMemory() {
        return mScratchMemory;
    }

}}  // namespace dawn_native::vulkan