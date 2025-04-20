//
// Created by ZZK on 2024/8/7.
//

#pragma once

#include "DirectXTex.h"
#include <wrl/client.h>

namespace DirectX
{
    // Helper structure
    struct CaptureTextureDesc
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> pStaging = nullptr;
        std::unique_ptr<uint8_t[]> layoutBuff = nullptr;
        D3D12_RESOURCE_DESC capturedResourceDesc = {};
        uint32_t numberOfPlanes = 1;
        uint32_t numberOfResources = 1;
        bool isCubeMap = false;
    };

    // Copy texture (include all subresources) to staging buffer, only record commands, support depth-stencil buffer by setting enableDepthCheck = false.
    HRESULT __cdecl CaptureTextureDeferred(
            _In_ ID3D12Device *device, _In_ ID3D12GraphicsCommandList *pCommandList, _In_ ID3D12Resource *pSource,
            _In_ CaptureTextureDesc &captureTextureDesc, _In_ bool isCubeMap, _In_ bool enableDepthCheck = true,
            _In_ D3D12_RESOURCE_STATES beforeState = D3D12_RESOURCE_STATE_RENDER_TARGET,
            _In_ D3D12_RESOURCE_STATES afterState = D3D12_RESOURCE_STATE_RENDER_TARGET) noexcept;

    // Copy texture subresource to staging buffer, only record commands.
    HRESULT __cdecl CaptureTextureSubresourceDeferred(
            _In_ ID3D12Device *device, _In_ ID3D12GraphicsCommandList *pCommandList, _In_ ID3D12Resource *pSource,
            _In_ const D3D12_RENDER_TARGET_VIEW_DESC &rtvDesc, _In_ CaptureTextureDesc &captureTextureDesc, _In_ bool isCubeMap,
            _In_ D3D12_RESOURCE_STATES beforeState = D3D12_RESOURCE_STATE_RENDER_TARGET,
            _In_ D3D12_RESOURCE_STATES afterState = D3D12_RESOURCE_STATE_RENDER_TARGET) noexcept;

    // Copy buffer to staging buffer, only record commands.
    HRESULT __cdecl CaptureBufferDeferred(
            _In_ ID3D12Device *device, _In_ ID3D12GraphicsCommandList *pCommandList, _In_ ID3D12Resource *pSource,
            _In_ CaptureTextureDesc &captureTextureDesc,
            _In_ D3D12_RESOURCE_STATES beforeState = D3D12_RESOURCE_STATE_RENDER_TARGET,
            _In_ D3D12_RESOURCE_STATES afterState = D3D12_RESOURCE_STATE_RENDER_TARGET) noexcept;

    // Copy buffer to staging buffer immediately, and dump staging buffer data as a binary file.
    HRESULT __cdecl CaptureBufferImmediately(_In_ ID3D12CommandQueue *pCommandQueue,
                                    _In_ ID3D12Resource *pSource,
                                    _In_z_ const wchar_t *szFile,
                                    _In_ D3D12_RESOURCE_STATES beforeState = D3D12_RESOURCE_STATE_RENDER_TARGET,
                                    _In_ D3D12_RESOURCE_STATES afterState = D3D12_RESOURCE_STATE_RENDER_TARGET) noexcept;

    // Dump staging buffer data as a dds-format file, must firstly invoke CaptureTextureDeferred function and wait for the commands to finish executing.
    HRESULT __cdecl SaveToDDSFileImmediately(_In_ const CaptureTextureDesc &captureTextureDesc, _In_ DDS_FLAGS flags, _In_z_ const wchar_t *szFile) noexcept;

    // Dump staging buffer data as a binary file, must firstly invoke CaptureBufferDeferred function and wait for the commands to finish executing.
    HRESULT __cdecl SaveToBinFileImmediately(_In_ const CaptureTextureDesc &captureTextureDesc, _In_z_ const wchar_t *szFile) noexcept;

    // Dump staging buffer data as a wic-format file, must firstly invoke CaptureTextureDeferred function and wait for the commands to finish executing.
    HRESULT __cdecl SaveToWICFileImmediately(_In_ const CaptureTextureDesc &captureTextureDesc, _In_ WIC_FLAGS flags, _In_ REFGUID guidContainerFormat,
                                        _In_z_ const wchar_t *szFile, _In_opt_ const GUID *targetFormat = nullptr,
                                        _In_ std::function<void __cdecl(IPropertyBag2*)> setCustomProps = nullptr) noexcept;
}