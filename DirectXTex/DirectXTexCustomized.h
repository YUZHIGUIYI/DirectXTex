//
// Created by ZZK on 2024/8/7.
//

#pragma once

#include "DirectXTex.h"

namespace DirectX
{
    struct CaptureTextureDesc
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> pStaging = nullptr;
        std::unique_ptr<uint8_t[]> layoutBuff = nullptr;
        D3D12_RESOURCE_DESC capturedResourceDesc = {};
        uint32_t numberOfPlanes = 1;
        uint32_t numberOfResources = 1;
        bool isCubeMap = false;
    };

    HRESULT __cdecl CaptureTextureDeferred(
            _In_ ID3D12Device* device, _In_ ID3D12GraphicsCommandList* pCommandList, _In_ ID3D12Resource* pSource,
            CaptureTextureDesc &captureTextureDesc, _In_ bool isCubeMap,
            _In_ D3D12_RESOURCE_STATES beforeState = D3D12_RESOURCE_STATE_RENDER_TARGET,
            _In_ D3D12_RESOURCE_STATES afterState = D3D12_RESOURCE_STATE_RENDER_TARGET) noexcept;

    HRESULT __cdecl CaptureBufferDeferred(
            _In_ ID3D12Device* device, _In_ ID3D12GraphicsCommandList* pCommandList, _In_ ID3D12Resource* pSource,
            CaptureTextureDesc &captureTextureDesc,
            _In_ D3D12_RESOURCE_STATES beforeState = D3D12_RESOURCE_STATE_RENDER_TARGET,
            _In_ D3D12_RESOURCE_STATES afterState = D3D12_RESOURCE_STATE_RENDER_TARGET) noexcept;

    HRESULT __cdecl SaveToDDSFileImmediately(_In_ const CaptureTextureDesc& captureTextureDesc, _In_ DDS_FLAGS flags, _In_z_ const wchar_t* szFile) noexcept;

    HRESULT __cdecl SaveToBinFileImmediately(_In_ const CaptureTextureDesc& captureTextureDesc, _In_z_ const wchar_t* szFile) noexcept;
}