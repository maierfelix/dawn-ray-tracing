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
#include "dawn_native/vulkan/ResourceHeapVk.h"
#include "dawn_native/vulkan/StagingBufferVk.h"

#include "dawn_native/vulkan/DeviceVk.h"
#include "dawn_native/vulkan/UtilsVulkan.h"
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

        VkAccelerationStructureTypeNV VulkanAccelerationContainerLevel(
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

        VkBuildAccelerationStructureFlagBitsNV VulkanBuildAccelerationStructureFlags(
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

    }  // anonymous namespace

    // static
    ResultOrError<RayTracingAccelerationContainer*> RayTracingAccelerationContainer::Create(
        Device* device,
        const RayTracingAccelerationContainerDescriptor* descriptor) {
        std::unique_ptr<RayTracingAccelerationContainer> geometry =
            std::make_unique<RayTracingAccelerationContainer>(device, descriptor);
        DAWN_TRY(geometry->Initialize(descriptor));
        return geometry.release();
    }

    MaybeError RayTracingAccelerationContainer::Initialize(
        const RayTracingAccelerationContainerDescriptor* descriptor) {
        Device* device = ToBackend(GetDevice());

        // save container level
        mLevel = VulkanAccelerationContainerLevel(descriptor->level);
        mFlags = VulkanBuildAccelerationStructureFlags(descriptor->flags);

        // validate ray tracing calls
        if (device->fn.CreateAccelerationStructureNV == nullptr) {
            return DAWN_VALIDATION_ERROR("Invalid Call to CreateAccelerationStructureNV");
        }

        // acceleration container holds geometry
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Bottom) {
            for (unsigned int ii = 0; ii < descriptor->geometryCount; ++ii) {
                RayTracingAccelerationGeometryDescriptor geomDsc = descriptor->geometries[ii];

                VkGeometryNV geometryInfo{};

                // for now, we lock the geometry type to triangle-only
                if (geomDsc.type != wgpu::RayTracingAccelerationGeometryType::Triangles) {
                    return DAWN_VALIDATION_ERROR(
                        "Other Geometry types than 'Triangles' is unsupported");
                }

                Buffer* vertexBuffer = ToBackend(geomDsc.vertexBuffer);
                if (vertexBuffer->GetHandle() == VK_NULL_HANDLE) {
                    return DAWN_VALIDATION_ERROR(
                        "Invalid vertex data");
                }

                Buffer* indexBuffer = ToBackend(geomDsc.indexBuffer);
                if (indexBuffer->GetHandle() == VK_NULL_HANDLE) {
                    return DAWN_VALIDATION_ERROR("Invalid index data");
                }

                geometryInfo.pNext = nullptr;
                geometryInfo.sType = VK_STRUCTURE_TYPE_GEOMETRY_NV;
                geometryInfo.flags = VK_GEOMETRY_OPAQUE_BIT_NV;
                geometryInfo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_NV;
                // triangle
                geometryInfo.geometry.triangles.sType = VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV;
                geometryInfo.geometry.triangles.pNext = nullptr;
                geometryInfo.geometry.triangles.vertexData = vertexBuffer->GetHandle();
                geometryInfo.geometry.triangles.vertexOffset = 0;
                geometryInfo.geometry.triangles.vertexCount = 9;
                geometryInfo.geometry.triangles.vertexStride = 3 * sizeof(float);
                geometryInfo.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
                geometryInfo.geometry.triangles.indexData = indexBuffer->GetHandle();
                geometryInfo.geometry.triangles.indexOffset = 0;
                geometryInfo.geometry.triangles.indexCount = 3;
                geometryInfo.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
                geometryInfo.geometry.triangles.transformData = nullptr;
                geometryInfo.geometry.triangles.transformOffset = 0;
                // aabb
                geometryInfo.geometry.aabbs.sType = VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV;
                geometryInfo.geometry.aabbs.pNext = nullptr;
                geometryInfo.geometry.aabbs.aabbData = VK_NULL_HANDLE;

                mGeometries.push_back(geometryInfo);
            };
        }

        // create the acceleration container
        {
            MaybeError result = CreateAccelerationStructure(descriptor);
            if (result.IsError())
                return result.AcquireError();
        }

        // acceleration container holds instances
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
            // create data for instance buffer
            for (unsigned int ii = 0; ii < descriptor->instanceCount; ++ii) {
                RayTracingAccelerationInstanceDescriptor instance = descriptor->instances[ii];
                RayTracingAccelerationContainer* geometryContainer =
                    ToBackend(instance.geometryContainer);
                VkAccelerationInstance instanceData{};
                memcpy(&instanceData.transform, const_cast<float*>(instance.transform),
                       sizeof(instanceData.transform));
                instanceData.instanceId = instance.instanceId;
                instanceData.mask = 0xFF;
                instanceData.instanceOffset = 0;
                instanceData.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV;
                instanceData.accelerationStructureHandle = geometryContainer->GetHandle();
                if (geometryContainer->GetHandle() == 0) {
                    return DAWN_VALIDATION_ERROR("Invalid Acceleration Container Handle");
                }
                mInstances.push_back(instanceData);
            };
        }

        // container requires instance buffer
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
            uint64_t bufferSize = descriptor->instanceCount * sizeof(VkAccelerationInstance);

            VkBufferCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            createInfo.pNext = nullptr;
            createInfo.flags = 0;
            createInfo.size = bufferSize;
            createInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;
            createInfo.pQueueFamilyIndices = 0;

            DAWN_TRY(CheckVkSuccess(device->fn.CreateBuffer(device->GetVkDevice(), &createInfo,
                                                            nullptr, &mInstanceBuffer),
                                    "vkCreateBuffer"));

            VkMemoryRequirements requirements;
            device->fn.GetBufferMemoryRequirements(device->GetVkDevice(), mInstanceBuffer,
                                                   &requirements);

            DAWN_TRY_ASSIGN(mInstanceResource, device->AllocateMemory(requirements, true));

            DAWN_TRY(CheckVkSuccess(device->fn.BindBufferMemory(
                                        device->GetVkDevice(), mInstanceBuffer,
                                        ToBackend(mInstanceResource.GetResourceHeap())->GetMemory(),
                                        mInstanceResource.GetOffset()),
                                    "vkBindBufferMemory"));

            // copy instance data into instance buffer
            memcpy(mInstanceResource.GetMappedPointer(), mInstances.data(), bufferSize);
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
        Device* device = ToBackend(GetDevice());
        if (mAccelerationStructure != VK_NULL_HANDLE) {
            device->fn.DestroyAccelerationStructureNV(device->GetVkDevice(), mAccelerationStructure,
                                                      nullptr);
            mAccelerationStructure = VK_NULL_HANDLE;
        }
        mScratchMemory = {};
    }

    MaybeError RayTracingAccelerationContainer::ReserveScratchMemory(
        const RayTracingAccelerationContainerDescriptor* descriptor) {
        Device* device = ToBackend(GetDevice());

        // create scratch memory for this container
        uint32_t resultSize =
            GetMemoryRequirementSize(VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV);
        uint32_t buildSize = GetMemoryRequirementSize(
            VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV);
        uint32_t updateSize = GetMemoryRequirementSize(
            VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_UPDATE_SCRATCH_NV);

        {
            // allocate scratch result memory
            VkMemoryRequirements2 resultRequirements =
                GetMemoryRequirements(VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV);
            resultRequirements.memoryRequirements.size = resultSize;
            DAWN_TRY_ASSIGN(mScratchMemory.result.resource,
                            device->AllocateMemory(resultRequirements.memoryRequirements, false));
            mScratchMemory.result.offset = mScratchMemory.result.resource.GetOffset();

            // allocate scratch build memory
            VkMemoryRequirements2 buildRequirements = GetMemoryRequirements(
                VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV);
            buildRequirements.memoryRequirements.size = buildSize;
            DAWN_TRY_ASSIGN(mScratchMemory.build.resource,
                            device->AllocateMemory(buildRequirements.memoryRequirements, false));
            mScratchMemory.build.offset = mScratchMemory.build.resource.GetOffset();
            {
                MaybeError result = CreateBufferFromResourceMemoryAllocation(
                    device, &mScratchMemory.build.buffer, buildSize,
                    VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
                    mScratchMemory.build.resource);
                if (result.IsError())
                    return result;
            }

            // allocate scratch update memory (if necessary)
            VkMemoryRequirements2 updateRequirements = GetMemoryRequirements(
                VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_UPDATE_SCRATCH_NV);
            updateRequirements.memoryRequirements.size = updateSize;
            if (updateRequirements.memoryRequirements.size > 0) {
                DAWN_TRY_ASSIGN(
                    mScratchMemory.update.resource,
                    device->AllocateMemory(updateRequirements.memoryRequirements, false));
                mScratchMemory.update.offset = mScratchMemory.update.resource.GetOffset();
            }

        }

        // bind scratch result memory
        VkBindAccelerationStructureMemoryInfoNV memoryBindInfo{};
        memoryBindInfo.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
        memoryBindInfo.accelerationStructure = GetAccelerationStructure();
        memoryBindInfo.memory =
            ToBackend(mScratchMemory.result.resource.GetResourceHeap())->GetMemory();
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
        memoryRequirementsInfo.accelerationStructure = mAccelerationStructure;

        VkMemoryRequirements2 memoryRequirements2{};
        memoryRequirements2.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
        memoryRequirementsInfo.type = type;

        device->fn.GetAccelerationStructureMemoryRequirementsNV(
            device->GetVkDevice(), &memoryRequirementsInfo, &memoryRequirements2);

        return memoryRequirements2;
    }

    uint32_t RayTracingAccelerationContainer::GetMemoryRequirementSize(
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
            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV;
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
            accelerationStructureCI.info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
            accelerationStructureCI.info.instanceCount = descriptor->instanceCount;
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
                device->GetVkDevice(), &accelerationStructureCI, nullptr, &mAccelerationStructure),
            "vkCreateAccelerationStructureNV");
        if (result.IsError())
            return result;

        return {};
    }

    VkBuffer RayTracingAccelerationContainer::GetInstanceBufferHandle() const {
        return mInstanceBuffer;
    }

    uint32_t RayTracingAccelerationContainer::GetInstanceBufferOffset() const {
        return mInstanceResource.GetOffset();
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

    uint64_t RayTracingAccelerationContainer::GetHandle() const {
        return mHandle;
    }

    VkAccelerationStructureTypeNV RayTracingAccelerationContainer::GetLevel() const {
        return mLevel;
    }

    VkBuildAccelerationStructureFlagBitsNV RayTracingAccelerationContainer::GetFlags() const {
        return mFlags;
    }

    VkAccelerationStructureNV RayTracingAccelerationContainer::GetAccelerationStructure() const {
        return mAccelerationStructure;
    }

    std::vector<VkGeometryNV>& RayTracingAccelerationContainer::GetGeometries() {
        return mGeometries;
    }

    std::vector<VkAccelerationInstance>& RayTracingAccelerationContainer::GetInstances() {
        return mInstances;
    }

    ScratchMemoryPool RayTracingAccelerationContainer::GetScratchMemory() const {
        return mScratchMemory;
    }

}}  // namespace dawn_native::vulkan
