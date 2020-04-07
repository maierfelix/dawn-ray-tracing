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

        D3D12_RAYTRACING_INSTANCE_DESC GetD3D12AccelerationInstance(
            const RayTracingAccelerationInstanceDescriptor& descriptor) {
            /*RayTracingAccelerationContainer* geometryContainer =
                ToBackend(descriptor.geometryContainer);*/
            D3D12_RAYTRACING_INSTANCE_DESC out;
            // process transform object
            if (descriptor.transform != nullptr) {
                float transform[16] = {};
                Fill4x3TransformMatrix(transform, descriptor.transform->translation,
                                       descriptor.transform->rotation, descriptor.transform->scale);
                memcpy(&out.Transform, transform, sizeof(out.Transform));
            }
            // process transform matrix
            else if (descriptor.transformMatrix != nullptr) {
                memcpy(&out.Transform, descriptor.transformMatrix, sizeof(out.Transform));
            }
            out.InstanceID = descriptor.instanceId;
            out.InstanceMask = descriptor.mask;
            out.InstanceContributionToHitGroupIndex = descriptor.instanceOffset;
            out.Flags = ToD3D12RayTracingInstanceFlags(descriptor.flags);
            // out.AccelerationStructure = geometryContainer->GetHandle();
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
    }

    MaybeError RayTracingAccelerationContainer::Initialize(
        const RayTracingAccelerationContainerDescriptor* descriptor) {
        Device* device = ToBackend(GetDevice());

        // TODO: make this an extension
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 rtFeatures;
        DAWN_TRY(CheckHRESULT(device->GetD3D12Device()->CheckFeatureSupport(
                                  D3D12_FEATURE_D3D12_OPTIONS5, &rtFeatures,
                                  sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5)),
                              "Failed to check device for ray tracing support"));
        if (rtFeatures.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
            return DAWN_VALIDATION_ERROR("Ray tracing is not supported on this device");
        }

        // acceleration container holds geometry
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Bottom) {
            mGeometries.reserve(descriptor->geometryCount);
            for (unsigned int ii = 0; ii < descriptor->geometryCount; ++ii) {
                const RayTracingAccelerationGeometryDescriptor& geometry =
                    descriptor->geometries[ii];
                D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc;
                geometryDesc.Type = ToD3D12RayTracingGeometryType(geometry.type);
                geometryDesc.Flags = ToD3D12RayTracingGeometryFlags(geometry.flags);

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
                } else {
                    geometryDesc.Triangles.VertexBuffer.StartAddress = 0;
                    geometryDesc.Triangles.VertexBuffer.StrideInBytes = 0;
                    geometryDesc.Triangles.VertexCount = 0;
                    geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_UNKNOWN;
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
                } else {
                    geometryDesc.Triangles.IndexBuffer = 0;
                    geometryDesc.Triangles.IndexCount = 0;
                    geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
                }
                // aabb buffer
                if (geometry.aabb != nullptr && geometry.aabb->buffer != nullptr) {
                    Buffer* aabbBuffer = ToBackend(geometry.aabb->buffer);
                    geometryDesc.AABBs.AABBs.StartAddress =
                        aabbBuffer->GetD3D12Resource()->GetGPUVirtualAddress() +
                        geometry.aabb->offset;
                    geometryDesc.AABBs.AABBCount = geometry.aabb->count;
                    geometryDesc.AABBs.AABBs.StrideInBytes =
                        static_cast<uint64_t>(geometry.vertex->stride);
                } else {
                    geometryDesc.AABBs.AABBs.StartAddress = 0;
                    geometryDesc.AABBs.AABBCount = 0;
                    geometryDesc.AABBs.AABBs.StrideInBytes = 0;
                }
                mGeometries.push_back(geometryDesc);
            };
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
            };
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

            // copy instance data into instance buffer
            buffer->SetSubData(0, bufferSize, mInstances.data());
            mInstanceCount = mInstances.size();
        }

        // reserve scratch memory
        /*{
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
            inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            inputs.Flags = ToD3D12RayTracingAccelerationStructureBuildFlags(descriptor->flags);
            inputs.NumDescs = 1;
            inputs.pGeometryDescs = mGeometries.data();
            inputs.Type = ToD3D12RayTracingAccelerationContainerLevel(descriptor->level);

            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
            device->GetD3D12Device()->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
        }*/

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