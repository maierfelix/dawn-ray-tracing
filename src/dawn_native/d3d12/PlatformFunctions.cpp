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

#include "dawn_native/d3d12/PlatformFunctions.h"

#include "common/DynamicLib.h"

namespace dawn_native { namespace d3d12 {

    PlatformFunctions::PlatformFunctions() {
    }
    PlatformFunctions::~PlatformFunctions() {
    }

    MaybeError PlatformFunctions::LoadFunctions() {
        DAWN_TRY(LoadModuleDirectory());
        DAWN_TRY(LoadD3D12());
        DAWN_TRY(LoadDXGI());
        DAWN_TRY(LoadDXCompiler());
        DAWN_TRY(LoadFXCompiler());
        DAWN_TRY(LoadD3D11());
        LoadPIXRuntime();
        return {};
    }

    MaybeError PlatformFunctions::LoadD3D12() {
        std::string error;
        if (!mD3D12Lib.Open("d3d12.dll", &error) ||
            !mD3D12Lib.GetProc(&d3d12CreateDevice, "D3D12CreateDevice", &error) ||
            !mD3D12Lib.GetProc(&d3d12GetDebugInterface, "D3D12GetDebugInterface", &error) ||
            !mD3D12Lib.GetProc(&d3d12SerializeRootSignature, "D3D12SerializeRootSignature",
                               &error) ||
            !mD3D12Lib.GetProc(&d3d12CreateRootSignatureDeserializer,
                               "D3D12CreateRootSignatureDeserializer", &error) ||
            !mD3D12Lib.GetProc(&d3d12SerializeVersionedRootSignature,
                               "D3D12SerializeVersionedRootSignature", &error) ||
            !mD3D12Lib.GetProc(&d3d12CreateVersionedRootSignatureDeserializer,
                               "D3D12CreateVersionedRootSignatureDeserializer", &error)) {
            return DAWN_INTERNAL_ERROR(error.c_str());
        }

        return {};
    }

    MaybeError PlatformFunctions::LoadD3D11() {
        std::string error;
        if (!mD3D11Lib.Open("d3d11.dll", &error) ||
            !mD3D11Lib.GetProc(&d3d11on12CreateDevice, "D3D11On12CreateDevice", &error)) {
            return DAWN_INTERNAL_ERROR(error.c_str());
        }

        return {};
    }

    MaybeError PlatformFunctions::LoadDXGI() {
        std::string error;
        if (!mDXGILib.Open("dxgi.dll", &error) ||
            !mDXGILib.GetProc(&dxgiGetDebugInterface1, "DXGIGetDebugInterface1", &error) ||
            !mDXGILib.GetProc(&createDxgiFactory2, "CreateDXGIFactory2", &error)) {
            return DAWN_INTERNAL_ERROR(error.c_str());
        }

        return {};
    }

    MaybeError PlatformFunctions::LoadDXCompiler() {
        std::string error;
        bool dxilAvailable = mDXCompilerLib.Open(mModulePath + "\\dxil.dll", &error);
        // Do not throw if failed, DXC is optional
        if (mDXCompilerLib.Open(mModulePath + "\\dxcompiler.dll", &error)) {
            // Only load procs when DXC is available
            if (!mDXCompilerLib.GetProc(&dxcCreateInstance, "DxcCreateInstance", &error)) {
                return DAWN_INTERNAL_ERROR(error.c_str());
            }
            // If dxcompiler is available, but dxil is not, throw
            if (!dxilAvailable) {
                return DAWN_INTERNAL_ERROR("DXIL is missing, but is required by DXC");
            }
        }
        return {};
    }

    MaybeError PlatformFunctions::LoadFXCompiler() {
        std::string error;
        if (!mFXCompilerLib.Open("d3dcompiler_47.dll", &error) ||
            !mFXCompilerLib.GetProc(&d3dCompile, "D3DCompile", &error)) {
            return DAWN_INTERNAL_ERROR(error.c_str());
        }

        return {};
    }

    MaybeError PlatformFunctions::LoadModuleDirectory() {
        if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                  GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              L"kernel32.dll", &mModuleHandle) == 0) {
            return DAWN_INTERNAL_ERROR("Failed to retrieve module handle");
        }
        char lpFilename[MAX_PATH];
        if (GetModuleFileNameA(mModuleHandle, lpFilename, sizeof(lpFilename)) == 0) {
            return DAWN_INTERNAL_ERROR("Failed to retrieve module name");
        }
        std::string moduleFilename = lpFilename;
        mModulePath = moduleFilename.substr(0, moduleFilename.find_last_of("\\/"));
        return {};
    }

    bool PlatformFunctions::IsPIXEventRuntimeLoaded() const {
        return mPIXEventRuntimeLib.Valid();
    }

    void PlatformFunctions::LoadPIXRuntime() {
        if (!mPIXEventRuntimeLib.Open("WinPixEventRuntime.dll") ||
            !mPIXEventRuntimeLib.GetProc(&pixBeginEventOnCommandList,
                                         "PIXBeginEventOnCommandList") ||
            !mPIXEventRuntimeLib.GetProc(&pixEndEventOnCommandList, "PIXEndEventOnCommandList") ||
            !mPIXEventRuntimeLib.GetProc(&pixSetMarkerOnCommandList, "PIXSetMarkerOnCommandList")) {
            mPIXEventRuntimeLib.Close();
        }
    }

}}  // namespace dawn_native::d3d12
