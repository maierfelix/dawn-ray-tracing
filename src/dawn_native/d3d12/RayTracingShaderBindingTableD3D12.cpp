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
        /*mStages.reserve(descriptor->stagesCount);
        for (unsigned int ii = 0; ii < descriptor->stagesCount; ++ii) {
            RayTracingShaderBindingTableStagesDescriptor stage = descriptor->stages[ii];
        };

        mGroups.reserve(descriptor->groupsCount);
        for (unsigned int ii = 0; ii < descriptor->groupsCount; ++ii) {
            RayTracingShaderBindingTableGroupsDescriptor group = descriptor->groups[ii];

            mGroups.push_back(groupInfo);
        };*/

        uint32_t entrySize = 0;
        entrySize += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
        entrySize += 8;
        entrySize += Align(entrySize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

        uint32_t bufferSize = descriptor->groupsCount * entrySize;

        D3D12_RESOURCE_DESC resourceDescriptor;
        resourceDescriptor.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDescriptor.Alignment = 0;
        resourceDescriptor.Width = bufferSize;
        resourceDescriptor.Height = 1;
        resourceDescriptor.DepthOrArraySize = 1;
        resourceDescriptor.MipLevels = 1;
        resourceDescriptor.Format = DXGI_FORMAT_UNKNOWN;
        resourceDescriptor.SampleDesc.Count = 1;
        resourceDescriptor.SampleDesc.Quality = 0;
        resourceDescriptor.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDescriptor.Flags = D3D12_RESOURCE_FLAG_NONE;

        DAWN_TRY_ASSIGN(mGroupMemory.resource,
                        device->AllocateMemory(D3D12_HEAP_TYPE_UPLOAD, resourceDescriptor,
                                               D3D12_RESOURCE_STATE_GENERIC_READ));
        mGroupMemory.buffer = mGroupMemory.resource.GetD3D12Resource();

        /*void* pData = nullptr;

        D3D12_RANGE mapRange = {0, static_cast<size_t>(bufferSize)};
        DAWN_TRY(
            CheckHRESULT(mGroupMemory.buffer.Get()->Map(0, &mapRange, &pData), "D3D12 map failed"));

        if (pData == nullptr) {
            return DAWN_VALIDATION_ERROR("Failed to map SBT memory");
        }

        LPCWSTR mainEntry = L"main";
        // Entry 0 - ray-gen program ID and descriptor data
        memcpy((uint32_t*)pData + 0 * entrySize, mainEntry, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        // Entry 1 - miss program
        memcpy((uint32_t*)pData + 1 * entrySize, mainEntry, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        // Entry 2 - hit program
        memcpy((uint32_t*)pData + 2 * entrySize, mainEntry, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

        DescriptorHeapAllocator* descriptorHeapAllocator = device->GetDescriptorHeapAllocator();
        DescriptorHeapHandle dsvHeap;
        DAWN_TRY_ASSIGN(dsvHeap, descriptorHeapAllocator->AllocateGPUHeap(
                                     D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1));

        D3D12_GPU_DESCRIPTOR_HANDLE heapHandle =
            dsvHeap.Get()->GetGPUDescriptorHandleForHeapStart();
        uint64_t* addr =
            reinterpret_cast<uint64_t*>(&pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        addr[0] = heapHandle.ptr;*/

        return {};
    }

    RayTracingShaderBindingTable::~RayTracingShaderBindingTable() {
        DestroyInternal();
    }

}}  // namespace dawn_native::d3d12
