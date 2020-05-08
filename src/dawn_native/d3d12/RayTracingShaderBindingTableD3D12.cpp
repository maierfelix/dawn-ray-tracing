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
#include "dawn_native/d3d12/DeviceD3D12.h"
#include "dawn_native/d3d12/ShaderVisibleDescriptorAllocatorD3D12.h"

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
        for (unsigned int ii = 0; ii < descriptor->stagesCount; ++ii) {
            const RayTracingShaderBindingTableStagesDescriptor& stage = descriptor->stages[ii];
            mStages.push_back(stage);
        }
        for (unsigned int ii = 0; ii < descriptor->groupsCount; ++ii) {
            const RayTracingShaderBindingTableGroupsDescriptor& group = descriptor->groups[ii];
            mGroups.push_back(group);
        }
        return {};
    }

    RayTracingShaderBindingTable::~RayTracingShaderBindingTable() {
        DestroyInternal();
    }

    uint32_t RayTracingShaderBindingTable::GetTableSize() const {
        return mTableSize;
    }

    ComPtr<ID3D12Resource> RayTracingShaderBindingTable::GetTableBuffer() {
        return mTableBuffer;
    }

    MaybeError RayTracingShaderBindingTable::Generate(RayTracingPipeline* pipeline,
                                                      PipelineLayout* pipelineLayout) {
        Device* device = ToBackend(GetDevice());

        uint32_t genSectionSize = 0;
        uint32_t hitSectionSize = 0;
        uint32_t missSectionSize = 0;
        for (unsigned int ii = 0; ii < mGroups.size(); ++ii) {
            RayTracingShaderBindingTableGroupsDescriptor& group = mGroups[ii];
            // we don't use local root sigs yet, so the entry size is the same for all entries
            uint32_t baseEntrySize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
            // general
            if (group.generalIndex != -1) {
                auto& stage = mStages.at(group.generalIndex);
                // gen
                if (stage.stage == wgpu::ShaderStage::RayGeneration) {
                    genSectionSize += baseEntrySize;
                }
                // miss
                else if (stage.stage == wgpu::ShaderStage::RayMiss) {
                    missSectionSize += baseEntrySize;
                }
            }
            // hit
            else {
                hitSectionSize += baseEntrySize;
            }
        }
        // align each section
        genSectionSize = Align(genSectionSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
        hitSectionSize = Align(hitSectionSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
        missSectionSize = Align(missSectionSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

        mTableSize = genSectionSize + hitSectionSize + missSectionSize;

        D3D12_RESOURCE_DESC resourceDesc;
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Alignment = 0;
        resourceDesc.Width = mTableSize;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.SampleDesc.Quality = 0;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        DAWN_TRY_ASSIGN(mTableResource, device->AllocateMemory(D3D12_HEAP_TYPE_UPLOAD, resourceDesc,
                                                               D3D12_RESOURCE_STATE_GENERIC_READ));
        mTableBuffer = mTableResource.GetD3D12Resource();

        // Map the SBT
        uint8_t* pData;
        DAWN_TRY(CheckHRESULT(mTableBuffer->Map(0, nullptr, (void**)&pData), "Map SBT"));

        uint32_t offset = 0;
        for (unsigned int ii = 0; ii < mGroups.size(); ++ii) {
            memcpy(pData + offset, pipeline->GetShaderIdentifier(ii),
                   D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
            offset = Align(offset + mTableSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
        }

        // Unmap the SBT
        mTableBuffer->Unmap(0, nullptr);
        return {};
    }

    std::vector<RayTracingShaderBindingTableStagesDescriptor>&
    RayTracingShaderBindingTable::GetStages() {
        return mStages;
    }

    std::vector<RayTracingShaderBindingTableGroupsDescriptor>&
    RayTracingShaderBindingTable::GetGroups() {
        return mGroups;
    }

}}  // namespace dawn_native::d3d12
