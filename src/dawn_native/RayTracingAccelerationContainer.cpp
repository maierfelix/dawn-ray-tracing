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

    }

    MaybeError ValidateRayTracingAccelerationContainerDescriptor(DeviceBase* device, const RayTracingAccelerationContainerDescriptor* descriptor) {
        if (descriptor->level != wgpu::RayTracingAccelerationContainerLevel::Top &&
            descriptor->level != wgpu::RayTracingAccelerationContainerLevel::Bottom) {
            return DAWN_VALIDATION_ERROR("Invalid Acceleration Container Level. Must be Top or Bottom");
        }
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
            if (descriptor->geometryCount > 0) {
                return DAWN_VALIDATION_ERROR("Geometry Count for Top-Level Acceleration Container must be zero");
            }
            if (descriptor->instanceCount == 0) {
                return DAWN_VALIDATION_ERROR(
                    "No data provided for Top-Level Acceleration Container");
            }
            for (unsigned int ii = 0; ii < descriptor->instanceCount; ++ii) {
                const RayTracingAccelerationInstanceDescriptor& instance = descriptor->instances[ii];
                if (instance.geometryContainer == nullptr) {
                    return DAWN_VALIDATION_ERROR(
                        "Acceleration Container Instance requires a Geometry Container");
                }
            };
        }
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Bottom) {
            if (descriptor->instanceCount > 0) {
                return DAWN_VALIDATION_ERROR("Instance Count for Bottom-Level Acceleration Container must be zero");
            }
            if (descriptor->geometryCount == 0) {
                return DAWN_VALIDATION_ERROR(
                    "No data provided for Bottom-Level Acceleration Container");
            }
            for (unsigned int ii = 0; ii < descriptor->geometryCount; ++ii) {
                RayTracingAccelerationGeometryDescriptor geomDsc = descriptor->geometries[ii];

                // for now, we lock the supported geometry types to triangle-only
                if (geomDsc.type != wgpu::RayTracingAccelerationGeometryType::Triangles) {
                    return DAWN_VALIDATION_ERROR(
                        "Other Geometry types than 'Triangles' are not supported");
                }

                // geometry for acceleration containers doesn't have to be device local
                // but the performance for geometry on host memory is seriously slow
                // so we force users to provide the geometry buffers staged
                if ((geomDsc.vertexBuffer->GetUsage() & wgpu::BufferUsage::CopyDst) == 0) {
                    return DAWN_VALIDATION_ERROR("Vertex data must be staged");
                }
                if (geomDsc.indexBuffer != nullptr) {
                    if ((geomDsc.indexBuffer->GetUsage() & wgpu::BufferUsage::CopyDst) == 0) {
                        return DAWN_VALIDATION_ERROR("Index data must be staged");
                    }
                }
            };
        }
        return {};
    }

    RayTracingAccelerationContainerBase::RayTracingAccelerationContainerBase(DeviceBase* device, const RayTracingAccelerationContainerDescriptor* descriptor)
        : ObjectBase(device) {
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Bottom) {
            // save unique references to used vertex and index buffers
            for (unsigned int ii = 0; ii < descriptor->geometryCount; ++ii) {
                const RayTracingAccelerationGeometryDescriptor& geometry =
                    descriptor->geometries[ii];
                BufferBase* vertexBuffer = geometry.vertexBuffer;
                BufferBase* indexBuffer = geometry.indexBuffer;

                if (!VectorReferenceAlreadyExists(mVertexBuffers, vertexBuffer)) {
                    mVertexBuffers.push_back(vertexBuffer);
                }
                if (!VectorReferenceAlreadyExists(mIndexBuffers, indexBuffer)) {
                    mIndexBuffers.push_back(indexBuffer);
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
        return new RayTracingAccelerationContainerBase(device, ObjectBase::kError);
    }

    bool RayTracingAccelerationContainerBase::IsBuilt() const {
        return mIsBuilt;
    }

    bool RayTracingAccelerationContainerBase::IsUpdated() const {
        return mIsUpdated;
    }

    void RayTracingAccelerationContainerBase::SetBuildState(bool state) {
        mIsBuilt = state;
    }

    void RayTracingAccelerationContainerBase::SetUpdateState(bool state) {
        mIsUpdated = state;
    }

}  // namespace dawn_native