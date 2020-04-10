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

#include "dawn_native/d3d12/RayTracingShaderBindingTableD3D12.h"

#include "common/Math.h"
#include "dawn_native/Error.h"
#include "dawn_native/d3d12/AdapterD3D12.h"
#include "dawn_native/d3d12/D3D12Error.h"
#include "dawn_native/d3d12/DescriptorHeapAllocator.h"
#include "dawn_native/d3d12/DeviceD3D12.h"

namespace dawn_native { namespace d3d12 {

    namespace {

    }  // anonymous namespace

    // static
    ResultOrError<RayTracingShaderBindingTable*> RayTracingShaderBindingTable::Create(
        Device* device,
        const RayTracingShaderBindingTableDescriptor* descriptor) {
        std::unique_ptr<RayTracingShaderBindingTable> sbt =
            std::make_unique<RayTracingShaderBindingTable>(device, descriptor);
        DAWN_TRY(sbt->Initialize(descriptor));
        return sbt.release();
    }

    void RayTracingShaderBindingTable::DestroyImpl() {
    }

    MaybeError RayTracingShaderBindingTable::Initialize(
        const RayTracingShaderBindingTableDescriptor* descriptor) {
        Device* device = ToBackend(GetDevice());
        mStages.reserve(descriptor->stagesCount);
        for (unsigned int ii = 0; ii < descriptor->stagesCount; ++ii) {
            RayTracingShaderBindingTableStagesDescriptor stage = descriptor->stages[ii];
            mStages.push_back(ToBackend(stage.module));
        };

        uint32_t entrySize = Align(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + 8,
                                   D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

        uint32_t shaderTableSize = descriptor->groupsCount * entrySize;
        shaderTableSize = Align(shaderTableSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

        D3D12_RESOURCE_DESC resourceDescriptor;
        resourceDescriptor.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDescriptor.Alignment = 0;
        resourceDescriptor.Width = shaderTableSize;
        resourceDescriptor.Height = 1;
        resourceDescriptor.DepthOrArraySize = 1;
        resourceDescriptor.MipLevels = 1;
        resourceDescriptor.Format = DXGI_FORMAT_UNKNOWN;
        resourceDescriptor.SampleDesc.Count = 1;
        resourceDescriptor.SampleDesc.Quality = 0;
        resourceDescriptor.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDescriptor.Flags = D3D12_RESOURCE_FLAG_NONE;

        DAWN_TRY_ASSIGN(mMemory.resource,
                        device->AllocateMemory(D3D12_HEAP_TYPE_UPLOAD, resourceDescriptor,
                                               D3D12_RESOURCE_STATE_GENERIC_READ));
        mMemory.buffer = mMemory.resource.GetD3D12Resource();

        void* pData = nullptr;

        D3D12_RANGE mapRange = {0, static_cast<size_t>(shaderTableSize)};
        DAWN_TRY(CheckHRESULT(mMemory.buffer.Get()->Map(0, &mapRange, &pData), "D3D12 map failed"));

        if (pData == nullptr) {
            return DAWN_VALIDATION_ERROR("Failed to map SBT memory");
        }

        return {};
    }

    RayTracingShaderBindingTable::~RayTracingShaderBindingTable() {
        DestroyInternal();
    }

    MemoryEntry& RayTracingShaderBindingTable::GetMemory() {
        return mMemory;
    }

    std::vector<ShaderModule*>& RayTracingShaderBindingTable::GetStages() {
        return mStages;
    }

}}  // namespace dawn_native::d3d12
