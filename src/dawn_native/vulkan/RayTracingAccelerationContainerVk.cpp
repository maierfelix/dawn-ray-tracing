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

        VkAccelerationStructureTypeNV VulkanAccelerationContainerLevel(
            wgpu::RayTracingAccelerationContainerLevel containerLevel) {
            VkAccelerationStructureTypeNV level = static_cast<VkAccelerationStructureTypeNV>(0);
            if (containerLevel == wgpu::RayTracingAccelerationContainerLevel::Bottom) {
                level = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
            }
            if (containerLevel == wgpu::RayTracingAccelerationContainerLevel::Top) {
                level = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
            }
            return level;
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

        // acceleration container holds geometry
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Bottom) {
            for (unsigned int ii = 0; ii < descriptor->geometryCount; ++ii) {
                RayTracingAccelerationGeometryDescriptor geomDsc = descriptor->geometries[ii];
                VkGeometryNV geometryInfo = {VK_STRUCTURE_TYPE_GEOMETRY_NV};

                Buffer* vertexBuffer = ToBackend(geomDsc.vertexBuffer);
                geometryInfo.geometryType = VulkanGeometryType(geomDsc.type);

                // for now, we lock the geometry type to triangle-only
                if (geomDsc.type != wgpu::RayTracingAccelerationGeometryType::Triangles) {
                    return DAWN_VALIDATION_ERROR(
                        "Other Geometry types than 'Triangles' is unsupported");
                }

                geometryInfo.geometry.triangles.sType = VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV;
                geometryInfo.geometry.triangles.vertexData = vertexBuffer->GetHandle();
                geometryInfo.geometry.triangles.vertexOffset = geomDsc.vertexOffset;
                geometryInfo.geometry.triangles.vertexCount =
                    vertexBuffer->GetSize() / geomDsc.vertexStride;
                geometryInfo.geometry.triangles.vertexStride = geomDsc.vertexStride;
                geometryInfo.geometry.triangles.vertexFormat =
                    VulkanVertexFormat(geomDsc.vertexFormat);

                // index buffer is optional
                if (geomDsc.indexBuffer != nullptr &&
                    geomDsc.indexFormat != wgpu::IndexFormat::None) {
                    Buffer* indexBuffer = ToBackend(geomDsc.indexBuffer);
                    geometryInfo.geometry.triangles.indexData = indexBuffer->GetHandle();
                    geometryInfo.geometry.triangles.indexOffset = geomDsc.indexOffset;
                    geometryInfo.geometry.triangles.indexType =
                        VulkanIndexFormat(geomDsc.indexFormat);
                    geometryInfo.geometry.triangles.indexCount =
                        indexBuffer->GetSize() / VulkanIndexFormatStride(geomDsc.indexFormat);
                }

                geometryInfo.geometry.triangles.transformData = nullptr;
                geometryInfo.geometry.triangles.transformOffset = 0;
                geometryInfo.flags = VK_GEOMETRY_OPAQUE_BIT_NV;
                mGeometries.push_back(geometryInfo);
            };
        }

        // acceleration container holds geometry instances
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
            mInstances.reserve(descriptor->instanceCount);
            // create data for instance buffer
            for (unsigned int ii = 0; ii < descriptor->instanceCount; ++ii) {
                RayTracingAccelerationInstanceDescriptor instance = descriptor->instances[ii];
                RayTracingAccelerationContainer* geometryContainer = ToBackend(instance.geometryContainer);
                VkAccelerationInstance instanceData = {};
                memcpy(&instanceData.transform, const_cast<float*>(instance.transform), sizeof(instanceData.transform));
                instanceData.instanceCustomIndex = static_cast<uint32_t>(instance.instanceId);
                instanceData.mask = static_cast<uint32_t>(instance.mask);
                instanceData.instanceOffset = static_cast<uint32_t>(instance.instanceOffset);
                instanceData.flags = static_cast<uint32_t>(instance.flags);
                instanceData.accelerationStructureHandle = geometryContainer->GetHandle();
                mInstances[ii] = instanceData;
            };

        }

        VkAccelerationStructureInfoNV accelerationStructureInfo{};
        accelerationStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
        accelerationStructureInfo.flags = VulkanBuildAccelerationStructureFlags(descriptor->flags);
        accelerationStructureInfo.type = mLevel;
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
            accelerationStructureInfo.geometryCount = 0;
            accelerationStructureInfo.instanceCount = descriptor->instanceCount;
        } else if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Bottom) {
            accelerationStructureInfo.instanceCount = 0;
            accelerationStructureInfo.geometryCount = descriptor->geometryCount;
            accelerationStructureInfo.pGeometries = mGeometries.data();
        } else {
            return DAWN_VALIDATION_ERROR("Invalid Acceleration Container Level");
        }

        VkAccelerationStructureCreateInfoNV accelerationStructureCI{};
        accelerationStructureCI.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
        accelerationStructureCI.info = accelerationStructureInfo;

        // validate ray tracing calls
        if (device->fn.CreateAccelerationStructureNV == nullptr) {
            return DAWN_VALIDATION_ERROR("Invalid Call to CreateAccelerationStructureNV");
        }
        if (device->fn.DestroyAccelerationStructureNV == nullptr) {
            return DAWN_VALIDATION_ERROR("Invalid Call to DestroyAccelerationStructureNV");
        }
        if (device->fn.GetAccelerationStructureHandleNV == nullptr) {
            return DAWN_VALIDATION_ERROR("Invalid Call to GetAccelerationStructureHandleNV");
        }
        if (device->fn.BindAccelerationStructureMemoryNV == nullptr) {
            return DAWN_VALIDATION_ERROR("Invalid Call to BindAccelerationStructureMemoryNV");
        }
        if (device->fn.GetAccelerationStructureMemoryRequirementsNV == nullptr) {
            return DAWN_VALIDATION_ERROR(
                "Invalid Call to GetAccelerationStructureMemoryRequirementsNV");
        }

        {
            MaybeError result = CheckVkSuccess(device->fn.CreateAccelerationStructureNV(
                                                   device->GetVkDevice(), &accelerationStructureCI,
                                                   nullptr, &mAccelerationStructure),
                                               "CreateAccelerationStructureNV");
            if (result.IsError())
                return result.AcquireError();
        }

        // create handle
        {
            MaybeError result = CheckVkSuccess(
                device->fn.GetAccelerationStructureHandleNV(
                    device->GetVkDevice(), mAccelerationStructure, sizeof(void*), &mHandle),
                "GetAccelerationStructureHandleNV");
            if (result.IsError())
                return result.AcquireError();
        }

        // reserve driver space for geometry containers
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
            // find all unique bottom-level containers
            std::vector<RayTracingAccelerationContainer*> geometryContainers;
            for (unsigned int ii = 0; ii < descriptor->instanceCount; ++ii) {
                RayTracingAccelerationInstanceDescriptor instDesc = descriptor->instances[ii];
                RayTracingAccelerationContainer* container = ToBackend(instDesc.geometryContainer);
                if (container == nullptr) {
                    return DAWN_VALIDATION_ERROR(
                        "Invalid Reference to RayTracingAccelerationContainer");
                }
                // only pick unique geometry containers
                if (std::find(geometryContainers.begin(), geometryContainers.end(), container) ==
                    geometryContainers.end()) {
                    geometryContainers.push_back(container);
                }
            };

            // find bottom-level scratch buffer dimensions
            uint32_t scratchBufferResultOffset = 0;
            uint32_t scratchBufferBuildOffset = 0;
            uint32_t scratchBufferUpdateOffset = 0;

            for (unsigned int ii = 0; ii < geometryContainers.size(); ++ii) {
                RayTracingAccelerationContainer* container = geometryContainers[ii];
                // get the required scratch buffer space for this container
                uint32_t resultSize = container->GetMemoryRequirementSize(
                    VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV);
                uint32_t buildSize = container->GetMemoryRequirementSize(
                    VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV);
                uint32_t updateSize = container->GetMemoryRequirementSize(
                    VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_UPDATE_SCRATCH_NV);
                // update the container's scratch buffer memory offsets
                container->mScratchBufferResultOffset = scratchBufferResultOffset;
                container->mScratchBufferBuildOffset = scratchBufferBuildOffset;
                container->mScratchBufferUpdateOffset = scratchBufferUpdateOffset;
                // offset steps
                scratchBufferResultOffset += resultSize;
                scratchBufferBuildOffset += buildSize;
                scratchBufferUpdateOffset += updateSize;
            };

            // allocate bottom-level scratch buffer result
            VkMemoryRequirements2 resultRequirements =
                GetMemoryRequirements(VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV);
            resultRequirements.memoryRequirements.size = scratchBufferResultOffset;
            DAWN_TRY_ASSIGN(mScratchResultMemoryAllocation,
                            device->AllocateMemory(resultRequirements.memoryRequirements, false));

            // allocate bottom-level scratch buffer build
            VkMemoryRequirements2 buildRequirements = GetMemoryRequirements(
                VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV);
            resultRequirements.memoryRequirements.size = scratchBufferBuildOffset;
            DAWN_TRY_ASSIGN(mScratchBuildMemoryAllocation,
                            device->AllocateMemory(buildRequirements.memoryRequirements, false));

            // allocate bottom-level scratch buffer update (if necessary)
            VkMemoryRequirements2 updateRequirements = GetMemoryRequirements(
                VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_UPDATE_SCRATCH_NV);
            resultRequirements.memoryRequirements.size = scratchBufferUpdateOffset;
            if (updateRequirements.memoryRequirements.size > 0) {
                DAWN_TRY_ASSIGN(
                    mScratchUpdateMemoryAllocation,
                    device->AllocateMemory(updateRequirements.memoryRequirements, false));
            }

            // bind scratch buffer to each geometry container
            for (unsigned int ii = 0; ii < geometryContainers.size(); ++ii) {
                RayTracingAccelerationContainer* container = geometryContainers[ii];

                // TODO: also bind build/update scratch buffer?
                VkBindAccelerationStructureMemoryInfoNV memoryBindInfo{};
                memoryBindInfo.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
                memoryBindInfo.accelerationStructure = container->GetAccelerationStructure();
                memoryBindInfo.memory =
                    ToBackend(mScratchResultMemoryAllocation.GetResourceHeap())->GetMemory();
                memoryBindInfo.memoryOffset = container->mScratchBufferResultOffset;

                MaybeError result = CheckVkSuccess(device->fn.BindAccelerationStructureMemoryNV(
                                                       device->GetVkDevice(), 1, &memoryBindInfo),
                                                   "GetAccelerationStructureHandleNV");
                if (result.IsError())
                    return result.AcquireError();
            };
        }

        // reserve and upload instance buffer
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
            //CommandRecordingContext* recordingContext = device->GetPendingRecordingContext();
            
        }

        // reserve driver space for top level container
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
            // get the required scratch buffer space for this container
            uint32_t resultSize = GetMemoryRequirementSize(
                VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV);
            uint32_t buildSize = GetMemoryRequirementSize(
                VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV);
            uint32_t updateSize = GetMemoryRequirementSize(
                VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_UPDATE_SCRATCH_NV);

            // allocate top-level scratch buffer result
            VkMemoryRequirements2 resultRequirements =
                GetMemoryRequirements(VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV);
            resultRequirements.memoryRequirements.size = resultSize;
            DAWN_TRY_ASSIGN(mScratchResultMemoryAllocation,
                            device->AllocateMemory(resultRequirements.memoryRequirements, false));

            // allocate top-level scratch buffer build
            VkMemoryRequirements2 buildRequirements = GetMemoryRequirements(
                VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV);
            resultRequirements.memoryRequirements.size = buildSize;
            DAWN_TRY_ASSIGN(mScratchBuildMemoryAllocation,
                            device->AllocateMemory(buildRequirements.memoryRequirements, false));

            // allocate top-level scratch buffer update (if necessary)
            VkMemoryRequirements2 updateRequirements = GetMemoryRequirements(
                VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_UPDATE_SCRATCH_NV);
            resultRequirements.memoryRequirements.size = updateSize;
            if (updateRequirements.memoryRequirements.size > 0) {
                DAWN_TRY_ASSIGN(
                    mScratchUpdateMemoryAllocation,
                    device->AllocateMemory(updateRequirements.memoryRequirements, false));
            }
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
    }

    uint64_t RayTracingAccelerationContainer::GetHandle() const {
        return mHandle;
    }

    VkAccelerationStructureTypeNV RayTracingAccelerationContainer::GetLevel() const {
        return mLevel;
    }

    VkAccelerationStructureNV RayTracingAccelerationContainer::GetAccelerationStructure() const {
        return mAccelerationStructure;
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

}}  // namespace dawn_native::vulkan
