// Copyright 2017 The Dawn Authors
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

#include "dawn_native/RayTracingAccelerationContainer.h"

#include "common/Assert.h"
#include "common/Math.h"
#include "dawn_native/Device.h"

#include "dawn_native/Buffer.h"

namespace dawn_native {

    // RayTracingAccelerationContainer

    namespace {

        template <typename T, typename E>
        bool VectorReferenceAlreadyExists(std::vector<Ref<T>> const& vec, E* el) {
            for (auto const& element : vec) {
                if (element.Get() == el) {
                    return true;
                }
            };
            return false;
        }

        class ErrorRayTracingAccelerationContainer : public RayTracingAccelerationContainerBase {
          public:
            ErrorRayTracingAccelerationContainer(DeviceBase* device)
                : RayTracingAccelerationContainerBase(device, ObjectBase::kError) {
            }

          private:
            void DestroyImpl() override {
                UNREACHABLE();
            }
        };

    }  // anonymous namespace

    MaybeError ValidateRayTracingAccelerationContainerDescriptor(
        DeviceBase* device,
        const RayTracingAccelerationContainerDescriptor* descriptor) {
        if (descriptor->level != wgpu::RayTracingAccelerationContainerLevel::Top &&
            descriptor->level != wgpu::RayTracingAccelerationContainerLevel::Bottom) {
            return DAWN_VALIDATION_ERROR(
                "Invalid Acceleration Container Level. Must be Top or Bottom");
        }
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
            if (descriptor->geometryCount > 0) {
                return DAWN_VALIDATION_ERROR(
                    "Geometry Count for Top-Level Acceleration Container must be zero");
            }
            if (descriptor->instanceCount == 0) {
                return DAWN_VALIDATION_ERROR(
                    "No data provided for Top-Level Acceleration Container");
            }
            for (unsigned int ii = 0; ii < descriptor->instanceCount; ++ii) {
                const RayTracingAccelerationInstanceDescriptor& instance =
                    descriptor->instances[ii];
                if (instance.geometryContainer == nullptr) {
                    return DAWN_VALIDATION_ERROR(
                        "Acceleration Container Instance requires a Geometry Container");
                }
                // linked geometry container must not be destroyed
                if (instance.geometryContainer->IsDestroyed()) {
                    return DAWN_VALIDATION_ERROR("Linked Geometry Container must not be destroyed");
                }
            };
        }
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Bottom) {
            if (descriptor->instanceCount > 0) {
                return DAWN_VALIDATION_ERROR(
                    "Instance Count for Bottom-Level Acceleration Container must be zero");
            }
            if (descriptor->geometryCount == 0) {
                return DAWN_VALIDATION_ERROR(
                    "No data provided for Bottom-Level Acceleration Container");
            }
            for (unsigned int ii = 0; ii < descriptor->geometryCount; ++ii) {
                RayTracingAccelerationGeometryDescriptor geometry = descriptor->geometries[ii];
                if (geometry.type == wgpu::RayTracingAccelerationGeometryType::Triangles) {
                    if (geometry.vertex == nullptr) {
                        return DAWN_VALIDATION_ERROR("No Vertex data provided");
                    }
                }
                else if (geometry.type == wgpu::RayTracingAccelerationGeometryType::Aabbs) {
                    if (geometry.aabb == nullptr) {
                        return DAWN_VALIDATION_ERROR("No AABB data provided");
                    }
                }
                // validate vertex input
                if (geometry.vertex != nullptr) {
                    if ((geometry.vertex->buffer->GetUsage() & wgpu::BufferUsage::CopyDst) == 0) {
                        return DAWN_VALIDATION_ERROR("Vertex data must be staged");
                    }
                    if (geometry.vertex->buffer->GetSize() == 0) {
                        return DAWN_VALIDATION_ERROR("Invalid Buffer for Vertex data");
                    }
                    if (geometry.vertex->count == 0) {
                        return DAWN_VALIDATION_ERROR("Vertex count must not be zero");
                    }
                }
                // validate index input
                if (geometry.index != nullptr) {
                    if (geometry.index == nullptr) {
                        return DAWN_VALIDATION_ERROR("Index data requires Vertex data");
                    }
                    if (geometry.index->buffer->GetSize() == 0) {
                        return DAWN_VALIDATION_ERROR("Invalid Buffer for Index data");
                    }
                    if ((geometry.index->buffer->GetUsage() & wgpu::BufferUsage::CopyDst) == 0) {
                        return DAWN_VALIDATION_ERROR("Index data must be staged");
                    }
                    if (geometry.index->count == 0) {
                        return DAWN_VALIDATION_ERROR("Index count must not be zero");
                    }
                }
                // validate aabb input
                if (geometry.aabb != nullptr) {
                    if (geometry.vertex != nullptr) {
                        return DAWN_VALIDATION_ERROR(
                            "AABB is not allowed to be combined with Vertex data");
                    }
                    if (geometry.index != nullptr) {
                        return DAWN_VALIDATION_ERROR(
                            "AABB is not allowed to be combined with Index data");
                    }
                    if (geometry.aabb->buffer->GetSize() == 0) {
                        return DAWN_VALIDATION_ERROR("Invalid Buffer for AABB data");
                    }
                    if ((geometry.aabb->buffer->GetUsage() & wgpu::BufferUsage::CopyDst) == 0) {
                        return DAWN_VALIDATION_ERROR("AABB data must be staged");
                    }
                    if (geometry.aabb->count == 0) {
                        return DAWN_VALIDATION_ERROR("AABB count must not be zero");
                    }
                }
                if (geometry.vertex == nullptr && geometry.index == nullptr &&
                    geometry.aabb == nullptr) {
                    return DAWN_VALIDATION_ERROR("No geometry data provided");
                }
            };
        }
        return {};
    }

    RayTracingAccelerationContainerBase::RayTracingAccelerationContainerBase(
        DeviceBase* device,
        const RayTracingAccelerationContainerDescriptor* descriptor)
        : ObjectBase(device) {
        mFlags = descriptor->flags;
        mLevel = descriptor->level;
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Bottom) {
            // save unique references to used vertex and index buffers
            for (unsigned int ii = 0; ii < descriptor->geometryCount; ++ii) {
                const RayTracingAccelerationGeometryDescriptor& geometry =
                    descriptor->geometries[ii];

                if (geometry.vertex != nullptr &&
                    !VectorReferenceAlreadyExists(mVertexBuffers, geometry.vertex->buffer)) {
                    mVertexBuffers.push_back(geometry.vertex->buffer);
                }
                if (geometry.index != nullptr &&
                    !VectorReferenceAlreadyExists(mIndexBuffers, geometry.index->buffer)) {
                    mIndexBuffers.push_back(geometry.index->buffer);
                }
                if (geometry.aabb != nullptr &&
                    !VectorReferenceAlreadyExists(mAABBBuffers, geometry.aabb->buffer)) {
                    mAABBBuffers.push_back(geometry.aabb->buffer);
                }
            };
        }
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
            // save unique references to used geometry containers
            for (unsigned int ii = 0; ii < descriptor->instanceCount; ++ii) {
                const RayTracingAccelerationInstanceDescriptor& instance =
                    descriptor->instances[ii];
                RayTracingAccelerationContainerBase* container = instance.geometryContainer;

                if (!VectorReferenceAlreadyExists(mGeometryContainers, container)) {
                    mGeometryContainers.push_back(container);
                }
            };
        }
    }

    RayTracingAccelerationContainerBase::RayTracingAccelerationContainerBase(DeviceBase* device, ObjectBase::ErrorTag tag)
        : ObjectBase(device, tag) {
    }

    // static
    RayTracingAccelerationContainerBase* RayTracingAccelerationContainerBase::MakeError(DeviceBase* device) {
        return new ErrorRayTracingAccelerationContainer(device);
    }

    void RayTracingAccelerationContainerBase::Destroy() {
        DestroyInternal();
    }

    void RayTracingAccelerationContainerBase::DestroyInternal() {
        if (!IsDestroyed()) {
            DestroyImpl();
        }
        SetDestroyState(true);
    }

    bool RayTracingAccelerationContainerBase::IsBuilt() const {
        return mIsBuilt;
    }

    bool RayTracingAccelerationContainerBase::IsUpdated() const {
        return mIsUpdated;
    }

    bool RayTracingAccelerationContainerBase::IsDestroyed() const {
        return mIsDestroyed;
    }

    void RayTracingAccelerationContainerBase::SetBuildState(bool state) {
        mIsBuilt = state;
    }

    void RayTracingAccelerationContainerBase::SetUpdateState(bool state) {
        mIsUpdated = state;
    }

    void RayTracingAccelerationContainerBase::SetDestroyState(bool state) {
        mIsDestroyed = state;
    }

    MaybeError RayTracingAccelerationContainerBase::ValidateCanUseInSubmitNow() const {
        ASSERT(!IsError());
        if (IsDestroyed()) {
            return DAWN_VALIDATION_ERROR("Destroyed acceleration container used in a submit");
        }
        return {};
    }

    wgpu::RayTracingAccelerationContainerFlag RayTracingAccelerationContainerBase::GetFlags()
        const {
        return mFlags;
    }

    wgpu::RayTracingAccelerationContainerLevel RayTracingAccelerationContainerBase::GetLevel()
        const {
        return mLevel;
    }

}  // namespace dawn_native