//
// Created by ZZK on 2024/8/7.
//

#include "DirectXTexCustomized.h"
#include <fstream>
#include <array>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wunused-macros"
#endif

#define D3DX12_NO_STATE_OBJECT_HELPERS
#define D3DX12_NO_CHECK_FEATURE_SUPPORT_CLASS
#ifdef _GAMING_XBOX_SCARLETT
#include <d3dx12_xs.h>
#elif (defined(_XBOX_ONE) && defined(_TITLE)) || defined(_GAMING_XBOX)
#include "d3dx12_x.h"
#elif !defined(_WIN32) || defined(USING_DIRECTX_HEADERS)
#include "directx/d3dx12.h"
#include "dxguids/dxguids.h"
#else
#include "d3dx12.h"
#endif

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifndef IID_GRAPHICS_PPV_ARGS
#define IID_GRAPHICS_PPV_ARGS(x) IID_PPV_ARGS(x)
#endif

namespace DirectX
{
    template<typename T, typename PT>
    void AdjustPlaneResource(
            _In_ DXGI_FORMAT fmt,
            _In_ size_t height,
            _In_ size_t slicePlane,
            _Inout_ T& res) noexcept
    {
        switch (static_cast<int>(fmt))
        {
            case DXGI_FORMAT_NV12:
            case DXGI_FORMAT_P010:
            case DXGI_FORMAT_P016:
            case XBOX_DXGI_FORMAT_D16_UNORM_S8_UINT:
            case XBOX_DXGI_FORMAT_R16_UNORM_X8_TYPELESS:
            case XBOX_DXGI_FORMAT_X16_TYPELESS_G8_UINT:
                if (!slicePlane)
                {
                    // Plane 0
                    res.SlicePitch = res.RowPitch * static_cast<PT>(height);
                }
                else
                {
                    // Plane 1
                    res.pData = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(res.pData) + res.RowPitch * PT(height));
                    res.SlicePitch = res.RowPitch * static_cast<PT>((height + 1) >> 1);
                }
                break;

            case DXGI_FORMAT_NV11:
                if (!slicePlane)
                {
                    // Plane 0
                    res.SlicePitch = res.RowPitch * static_cast<PT>(height);
                }
                else
                {
                    // Plane 1
                    res.pData = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(res.pData) + res.RowPitch * PT(height));
                    res.RowPitch = (res.RowPitch >> 1);
                    res.SlicePitch = res.RowPitch * static_cast<PT>(height);
                }
                break;

            default:
                break;
        }
    }

    inline void TransitionResource(
            _In_ ID3D12GraphicsCommandList* commandList,
            _In_ ID3D12Resource* resource,
            _In_ D3D12_RESOURCE_STATES stateBefore,
            _In_ D3D12_RESOURCE_STATES stateAfter)
    {
        assert(commandList != nullptr);
        assert(resource != nullptr);

        if (commandList == nullptr || resource == nullptr || stateBefore == stateAfter)
        {
            return;
        }

        D3D12_RESOURCE_BARRIER desc{};
        desc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        desc.Transition.pResource = resource;
        desc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        desc.Transition.StateBefore = stateBefore;
        desc.Transition.StateAfter = stateAfter;

        commandList->ResourceBarrier(1, &desc);
    }

    inline void TransitionSubresource(
            _In_ ID3D12GraphicsCommandList *commandList,
            _In_ ID3D12Resource *resource,
            _In_ const std::vector<uint32_t> &subresourceIndexStorage,
            _In_ D3D12_RESOURCE_STATES stateBefore,
            _In_ D3D12_RESOURCE_STATES stateAfter)
    {
        assert(commandList != nullptr);
        assert(resource != nullptr);

        if (commandList == nullptr || resource == nullptr || stateBefore == stateAfter)
        {
            return;
        }

        std::vector<D3D12_RESOURCE_BARRIER> barrierStorage{};
        barrierStorage.reserve(subresourceIndexStorage.size());
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = stateBefore;
        barrier.Transition.StateAfter = stateAfter;
        for (uint32_t i = 0u; i < subresourceIndexStorage.size(); ++i)
        {
            barrier.Transition.Subresource = subresourceIndexStorage[i];
            barrierStorage.emplace_back(barrier);
        }
        commandList->ResourceBarrier(static_cast<uint32_t>(barrierStorage.size()), barrierStorage.data());
    }

    // Core functions
    _Use_decl_annotations_
    HRESULT __cdecl CaptureTextureDeferred(
            _In_ ID3D12Device* device, _In_ ID3D12GraphicsCommandList* pCommandList, _In_ ID3D12Resource* pSource,
            _In_ CaptureTextureDesc &captureTextureDesc, _In_ bool isCubeMap, _In_ bool enableDepthCheck,
            _In_ D3D12_RESOURCE_STATES beforeState,
            _In_ D3D12_RESOURCE_STATES afterState) noexcept
    {
        using Microsoft::WRL::ComPtr;

        if (!device || !pCommandList || !pSource)
            return E_INVALIDARG;

#if defined(_MSC_VER) || !defined(_WIN32)
        auto const desc = pSource->GetDesc();
#else
        D3D12_RESOURCE_DESC tmpDesc;
        auto const& desc = *pSource->GetDesc(&tmpDesc);
#endif
        captureTextureDesc.isCubeMap = isCubeMap;
        captureTextureDesc.capturedResourceDesc = desc;
        captureTextureDesc.numberOfPlanes = D3D12GetFormatPlaneCount(device, desc.Format);
        if (!captureTextureDesc.numberOfPlanes)
            return E_INVALIDARG;

        if (enableDepthCheck && (captureTextureDesc.numberOfPlanes > 1) && IsDepthStencil(desc.Format))
        {
            // DirectX 12 uses planes for stencil, DirectX 11 does not
            return HRESULT_E_NOT_SUPPORTED;
        }

        captureTextureDesc.numberOfResources = (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) ? 1u : desc.DepthOrArraySize;
        captureTextureDesc.numberOfResources *= desc.MipLevels;
        captureTextureDesc.numberOfResources *= captureTextureDesc.numberOfPlanes;

        if (captureTextureDesc.numberOfResources > D3D12_REQ_SUBRESOURCES)
            return E_UNEXPECTED;

        const size_t memAlloc = (sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT) + sizeof(UINT) + sizeof(UINT64)) * captureTextureDesc.numberOfResources;
        if (memAlloc > SIZE_MAX)
            return E_UNEXPECTED;

        captureTextureDesc.layoutBuff.reset(new (std::nothrow) uint8_t[memAlloc]);
        if (!captureTextureDesc.layoutBuff)
            return E_OUTOFMEMORY;

        auto pLayout = reinterpret_cast<D3D12_PLACED_SUBRESOURCE_FOOTPRINT *>(captureTextureDesc.layoutBuff.get());
        auto pRowSizesInBytes = reinterpret_cast<UINT64 *>(pLayout + captureTextureDesc.numberOfResources);
        auto pNumRows = reinterpret_cast<UINT *>(pRowSizesInBytes + captureTextureDesc.numberOfResources);

        UINT64 totalResourceSize = 0;
        device->GetCopyableFootprints(&desc, 0, captureTextureDesc.numberOfResources, 0,
                                        pLayout, pNumRows, pRowSizesInBytes, &totalResourceSize);

        D3D12_HEAP_PROPERTIES sourceHeapProperties;
        HRESULT hr = pSource->GetHeapProperties(&sourceHeapProperties, nullptr);
        if (SUCCEEDED(hr) && sourceHeapProperties.Type == D3D12_HEAP_TYPE_READBACK)
        {
            // Handle case where the source is already a staging texture we can use directly
            captureTextureDesc.pStaging = pSource;
            pSource->AddRef();
            return S_OK;
        }

        const CD3DX12_HEAP_PROPERTIES defaultHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_HEAP_PROPERTIES readBackHeapProperties(D3D12_HEAP_TYPE_READBACK);

        // Readback resources must be buffers
        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.Height = 1;
        bufferDesc.Width = totalResourceSize;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;

        ComPtr<ID3D12Resource> copySource(pSource);
        if (desc.SampleDesc.Count > 1)
        {
            // MSAA content must be resolved before being copied to a staging texture
            auto descCopy = desc;
            descCopy.SampleDesc.Count = 1;
            descCopy.SampleDesc.Quality = 0;
            descCopy.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

            ComPtr<ID3D12Resource> pTemp;
            hr = device->CreateCommittedResource(
                    &defaultHeapProperties,
                    D3D12_HEAP_FLAG_NONE,
                    &descCopy,
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    nullptr,
                    IID_GRAPHICS_PPV_ARGS(pTemp.GetAddressOf()));
            if (FAILED(hr))
                return hr;

            assert(pTemp);

            DXGI_FORMAT fmt = desc.Format;
            if (IsTypeless(fmt))
            {
                // Assume a UNORM if it exists otherwise use FLOAT
                fmt = MakeTypelessUNORM(fmt);
                fmt = MakeTypelessFLOAT(fmt);
            }

            D3D12_FEATURE_DATA_FORMAT_SUPPORT formatInfo = { fmt, D3D12_FORMAT_SUPPORT1_NONE, D3D12_FORMAT_SUPPORT2_NONE };
            hr = device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &formatInfo, sizeof(formatInfo));
            if (FAILED(hr))
                return hr;

            if (!(formatInfo.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D))
                return E_FAIL;

            for (UINT plane = 0; plane < captureTextureDesc.numberOfPlanes; ++plane)
            {
                for (UINT item = 0; item < desc.DepthOrArraySize; ++item)
                {
                    for (UINT level = 0; level < desc.MipLevels; ++level)
                    {
                        const UINT index = D3D12CalcSubresource(level, item, plane, desc.MipLevels, desc.DepthOrArraySize);
                        pCommandList->ResolveSubresource(pTemp.Get(), index, pSource, index, fmt);
                    }
                }
            }

            copySource = pTemp;
        }

        // Create a staging texture
        hr = device->CreateCommittedResource(
                &readBackHeapProperties,
                D3D12_HEAP_FLAG_NONE,
                &bufferDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_GRAPHICS_PPV_ARGS(captureTextureDesc.pStaging.GetAddressOf()));
        if (FAILED(hr))
            return hr;

        assert(captureTextureDesc.pStaging != nullptr);

        // Transition the resource if necessary
        TransitionResource(pCommandList, pSource, beforeState, D3D12_RESOURCE_STATE_COPY_SOURCE);

        // Get the copy target location
        for (UINT j = 0; j < captureTextureDesc.numberOfResources; ++j)
        {
            const CD3DX12_TEXTURE_COPY_LOCATION copyDest(captureTextureDesc.pStaging.Get(), pLayout[j]);
            const CD3DX12_TEXTURE_COPY_LOCATION copySrc(copySource.Get(), j);
            pCommandList->CopyTextureRegion(&copyDest, 0, 0, 0, &copySrc, nullptr);
        }

        // Transition the resource to the next state
        TransitionResource(pCommandList, pSource, D3D12_RESOURCE_STATE_COPY_SOURCE, afterState);

        return S_OK;
    }

    _Use_decl_annotations_
    HRESULT __cdecl CaptureTextureSubresourceDeferred(
            _In_ ID3D12Device *device, _In_ ID3D12GraphicsCommandList *pCommandList, _In_ ID3D12Resource *pSource,
            _In_ const D3D12_RENDER_TARGET_VIEW_DESC &rtvDesc, _In_ CaptureTextureDesc &captureTextureDesc, _In_ bool isCubeMap,
            _In_ D3D12_RESOURCE_STATES beforeState,
            _In_ D3D12_RESOURCE_STATES afterState) noexcept
    {
        using Microsoft::WRL::ComPtr;

        if (!device || !pCommandList || !pSource)
        {
            return E_INVALIDARG;
        }

#if defined(_MSC_VER) || !defined(_WIN32)
        auto const desc = pSource->GetDesc();
#else
        D3D12_RESOURCE_DESC tmpDesc;
        auto const& desc = *pSource->GetDesc(&tmpDesc);
#endif
        D3D12_RESOURCE_DESC fakeResourceDesc = desc;
        captureTextureDesc.isCubeMap = isCubeMap;
        captureTextureDesc.capturedResourceDesc = desc;
        captureTextureDesc.numberOfPlanes = D3D12GetFormatPlaneCount(device, desc.Format);

        std::vector<uint32_t> subresourceIndexStorage{};
        switch (rtvDesc.ViewDimension)
        {
            case D3D12_RTV_DIMENSION_TEXTURE1D:
            {
                fakeResourceDesc.Width = std::max(static_cast<uint64_t>(desc.Width >> rtvDesc.Texture1D.MipSlice), 1ull);
                fakeResourceDesc.MipLevels = 1u;
                fakeResourceDesc.DepthOrArraySize = 1u;
                captureTextureDesc.numberOfResources = 1u;
                uint32_t subresourceIndex = D3D12CalcSubresource(rtvDesc.Texture1D.MipSlice, 0u, 0u, desc.MipLevels, desc.DepthOrArraySize);
                subresourceIndexStorage.emplace_back(subresourceIndex);
                break;
            }
            case D3D12_RTV_DIMENSION_TEXTURE1DARRAY:
            {
                fakeResourceDesc.Width = std::max(static_cast<uint64_t>(desc.Width >> rtvDesc.Texture1DArray.MipSlice), 1ull);
                fakeResourceDesc.MipLevels = 1u;
                fakeResourceDesc.DepthOrArraySize = static_cast<uint16_t>(rtvDesc.Texture1DArray.ArraySize);
                captureTextureDesc.numberOfResources = rtvDesc.Texture1DArray.ArraySize;
                subresourceIndexStorage.reserve(captureTextureDesc.numberOfResources);
                for (uint32_t i = 0u; i < captureTextureDesc.numberOfResources; ++i)
                {
                    uint32_t subresourceIndex = D3D12CalcSubresource(rtvDesc.Texture1DArray.MipSlice, rtvDesc.Texture1DArray.FirstArraySlice + i, 0u, desc.MipLevels, desc.DepthOrArraySize);
                    subresourceIndexStorage.emplace_back(subresourceIndex);
                }
                break;
            }
            case D3D12_RTV_DIMENSION_TEXTURE2D:
            {
                fakeResourceDesc.Width = std::max(static_cast<uint64_t>(desc.Width >> rtvDesc.Texture2D.MipSlice), 1ull);
                fakeResourceDesc.Height = std::max(static_cast<uint32_t>(desc.Height >> rtvDesc.Texture2D.MipSlice), 1u);
                fakeResourceDesc.MipLevels = 1u;
                fakeResourceDesc.DepthOrArraySize = 1u;
                captureTextureDesc.numberOfResources = 1u;
                uint32_t subresourceIndex = D3D12CalcSubresource(rtvDesc.Texture2D.MipSlice, 0u, 0u, desc.MipLevels, desc.DepthOrArraySize);
                subresourceIndexStorage.emplace_back(subresourceIndex);
                break;
            }
            case D3D12_RTV_DIMENSION_TEXTURE2DARRAY:
            {
                fakeResourceDesc.Width = std::max(static_cast<uint64_t>(desc.Width >> rtvDesc.Texture2DArray.MipSlice), 1ull);
                fakeResourceDesc.Height = std::max(static_cast<uint32_t>(desc.Height >> rtvDesc.Texture2DArray.MipSlice), 1u);
                fakeResourceDesc.MipLevels = 1u;
                fakeResourceDesc.DepthOrArraySize = static_cast<uint16_t>(rtvDesc.Texture2DArray.ArraySize);
                captureTextureDesc.numberOfResources = rtvDesc.Texture2DArray.ArraySize;
                subresourceIndexStorage.reserve(captureTextureDesc.numberOfResources);
                for (uint32_t i = 0u; i < captureTextureDesc.numberOfResources; ++i)
                {
                    uint32_t subresourceIndex = D3D12CalcSubresource(rtvDesc.Texture2DArray.MipSlice, rtvDesc.Texture2DArray.FirstArraySlice + i, 0u, desc.MipLevels, desc.DepthOrArraySize);
                    subresourceIndexStorage.emplace_back(subresourceIndex);
                }
                break;
            }
            case D3D12_RTV_DIMENSION_TEXTURE2DMS:
            {
                fakeResourceDesc.MipLevels = 1u;
                fakeResourceDesc.DepthOrArraySize = 1u;
                fakeResourceDesc.SampleDesc = DXGI_SAMPLE_DESC{ 1u, 0u };
                fakeResourceDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
                captureTextureDesc.numberOfResources = 1u;
                subresourceIndexStorage.emplace_back(0u);
                break;
            }
            case D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY:
            {
                fakeResourceDesc.MipLevels = 1u;
                fakeResourceDesc.DepthOrArraySize = static_cast<uint16_t>(rtvDesc.Texture2DMSArray.ArraySize);
                fakeResourceDesc.SampleDesc = DXGI_SAMPLE_DESC{ 1u, 0u };
                fakeResourceDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
                captureTextureDesc.numberOfResources = rtvDesc.Texture2DMSArray.ArraySize;
                subresourceIndexStorage.reserve(captureTextureDesc.numberOfResources);
                for (uint32_t i = 0u; i < captureTextureDesc.numberOfResources; ++i)
                {
                    uint32_t subresourceIndex = D3D12CalcSubresource(0u, rtvDesc.Texture2DMSArray.FirstArraySlice + i, 0u, desc.MipLevels, desc.DepthOrArraySize);
                    subresourceIndexStorage.emplace_back(subresourceIndex);
                }
                break;
            }
            case D3D12_RTV_DIMENSION_TEXTURE3D:
            {
                // TODO: experimental feature
                fakeResourceDesc.Width = std::max(static_cast<uint64_t>(desc.Width >> rtvDesc.Texture3D.MipSlice), 1ull);
                fakeResourceDesc.Height = std::max(static_cast<uint32_t>(desc.Height >> rtvDesc.Texture3D.MipSlice), 1u);
                fakeResourceDesc.DepthOrArraySize = std::max(static_cast<uint16_t>(desc.DepthOrArraySize >> rtvDesc.Texture3D.MipSlice), static_cast<uint16_t>(1u));
                fakeResourceDesc.MipLevels = 1u;
                fakeResourceDesc.DepthOrArraySize = static_cast<uint16_t>(rtvDesc.Texture3D.WSize);
                captureTextureDesc.numberOfResources = rtvDesc.Texture3D.WSize;
                subresourceIndexStorage.reserve(captureTextureDesc.numberOfResources);
                for (uint32_t i = 0u; i < captureTextureDesc.numberOfResources; ++i)
                {
                    uint32_t subresourceIndex = D3D12CalcSubresource(rtvDesc.Texture3D.MipSlice, 0u, rtvDesc.Texture3D.FirstWSlice + i, desc.MipLevels, desc.DepthOrArraySize);
                    subresourceIndexStorage.emplace_back(subresourceIndex);
                }
                break;
            }
            default:
            {
                assert(false && "Unsupported render target view dimension");
                break;
            }
        }

        captureTextureDesc.capturedResourceDesc = fakeResourceDesc;
        const size_t memAlloc = (sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT) + sizeof(UINT) + sizeof(UINT64)) * captureTextureDesc.numberOfResources;
        if (memAlloc > SIZE_MAX)
        {
            return E_UNEXPECTED;
        }

        captureTextureDesc.layoutBuff.reset(new (std::nothrow) uint8_t[memAlloc]);
        if (!captureTextureDesc.layoutBuff)
        {
            return E_OUTOFMEMORY;
        }

        auto *pLayout = reinterpret_cast<D3D12_PLACED_SUBRESOURCE_FOOTPRINT *>(captureTextureDesc.layoutBuff.get());
        auto *pRowSizesInBytes = reinterpret_cast<uint64_t *>(pLayout + captureTextureDesc.numberOfResources);
        auto *pNumRows = reinterpret_cast<uint32_t *>(pRowSizesInBytes + captureTextureDesc.numberOfResources);

        uint64_t totalResourceSize = 0u;
        std::vector<uint64_t> subresourceOffsetSizeStorage(captureTextureDesc.numberOfResources, 0u);
        for (uint32_t i = 0u; i < captureTextureDesc.numberOfResources; ++i)
        {
            uint64_t subresourceSize = 0u;
            device->GetCopyableFootprints(&desc, subresourceIndexStorage[i], 1u, 0u,
                                            &pLayout[i], &pNumRows[i], &pRowSizesInBytes[i], &subresourceSize);
            totalResourceSize += subresourceSize;
            if (i < (captureTextureDesc.numberOfResources - 1u))
            {
                subresourceOffsetSizeStorage[i + 1] = totalResourceSize;
            }
        }

        D3D12_HEAP_PROPERTIES sourceHeapProperties;
        HRESULT hr = pSource->GetHeapProperties(&sourceHeapProperties, nullptr);
        if (SUCCEEDED(hr) && sourceHeapProperties.Type == D3D12_HEAP_TYPE_READBACK)
        {
            // Handle case where the source is already a staging texture we can use directly
            captureTextureDesc.pStaging = pSource;
            pSource->AddRef();
            return S_OK;
        }

        const CD3DX12_HEAP_PROPERTIES defaultHeapProperties{ D3D12_HEAP_TYPE_DEFAULT };
        const CD3DX12_HEAP_PROPERTIES readBackHeapProperties{ D3D12_HEAP_TYPE_READBACK };

        // Readback resources must be buffers
        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.DepthOrArraySize = 1u;
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.Height = 1u;
        bufferDesc.Width = totalResourceSize;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufferDesc.MipLevels = 1u;
        bufferDesc.SampleDesc.Count = 1u;

        ComPtr<ID3D12Resource> copySource(pSource);
        if (desc.SampleDesc.Count > 1u)
        {
            // MSAA content must be resolved before being copied to a staging texture
            auto descCopy = desc;
            descCopy.SampleDesc.Count = 1u;
            descCopy.SampleDesc.Quality = 0u;
            descCopy.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

            ComPtr<ID3D12Resource> pTemp;
            hr = device->CreateCommittedResource(
                    &defaultHeapProperties,
                    D3D12_HEAP_FLAG_NONE,
                    &descCopy,
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    nullptr,
                    IID_GRAPHICS_PPV_ARGS(pTemp.GetAddressOf()));
            if (FAILED(hr))
            {
                return hr;
            }

            assert(pTemp);

            DXGI_FORMAT fmt = desc.Format;
            if (IsTypeless(fmt))
            {
                // Assume a UNORM if it exists otherwise use FLOAT
                fmt = MakeTypelessUNORM(fmt);
                fmt = MakeTypelessFLOAT(fmt);
            }

            D3D12_FEATURE_DATA_FORMAT_SUPPORT formatInfo{ fmt, D3D12_FORMAT_SUPPORT1_NONE, D3D12_FORMAT_SUPPORT2_NONE };
            hr = device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &formatInfo, sizeof(formatInfo));
            if (FAILED(hr))
            {
                return hr;
            }

            if (!(formatInfo.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D))
            {
                return E_FAIL;
            }

            for (uint32_t i = 0u; i < captureTextureDesc.numberOfResources; ++i)
            {
                pCommandList->ResolveSubresource(pTemp.Get(), subresourceIndexStorage[i], pSource, subresourceIndexStorage[i], fmt);
            }

            copySource = pTemp;
        }

        // Create a staging texture
        hr = device->CreateCommittedResource(
                &readBackHeapProperties,
                D3D12_HEAP_FLAG_NONE,
                &bufferDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_GRAPHICS_PPV_ARGS(captureTextureDesc.pStaging.GetAddressOf()));
        if (FAILED(hr))
        {
            return hr;
        }

        assert(captureTextureDesc.pStaging != nullptr);

        // Transition the resource subresources if necessary
        TransitionSubresource(pCommandList, pSource, subresourceIndexStorage, beforeState, D3D12_RESOURCE_STATE_COPY_SOURCE);

        // Get the copy target location
        for (uint32_t j = 0; j < captureTextureDesc.numberOfResources; ++j)
        {
            const CD3DX12_TEXTURE_COPY_LOCATION copyDest{ captureTextureDesc.pStaging.Get(), D3D12_PLACED_SUBRESOURCE_FOOTPRINT{ subresourceOffsetSizeStorage[j], pLayout[j].Footprint } };
            const CD3DX12_TEXTURE_COPY_LOCATION copySrc{ copySource.Get(), subresourceIndexStorage[j] };
            pCommandList->CopyTextureRegion(&copyDest, 0u, 0u, 0u, &copySrc, nullptr);
        }

        // Transition the resource subresources to the next state
        TransitionSubresource(pCommandList, pSource, subresourceIndexStorage, D3D12_RESOURCE_STATE_COPY_SOURCE, afterState);

        return S_OK;
    }

    _Use_decl_annotations_
    HRESULT GetScratchImageFromCapturedTexture(_In_ const CaptureTextureDesc &captureTextureDesc, _Out_ ScratchImage &scratchImageResult, _Out_ TexMetadata &texMetadataCache)
    {
        HRESULT hr = S_OK;
        auto *pLayout = reinterpret_cast<const D3D12_PLACED_SUBRESOURCE_FOOTPRINT *>(captureTextureDesc.layoutBuff.get());
        auto *pRowSizesInBytes = reinterpret_cast<const UINT64 *>(pLayout + captureTextureDesc.numberOfResources);
        auto *pNumRows = reinterpret_cast<const UINT *>(pRowSizesInBytes + captureTextureDesc.numberOfResources);
        auto desc = captureTextureDesc.capturedResourceDesc;

        switch (desc.Dimension)
        {
            case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            {
                TexMetadata mdata{};
                mdata.width = static_cast<size_t>(desc.Width);
                mdata.height = mdata.depth = 1;
                mdata.arraySize = desc.DepthOrArraySize;
                mdata.mipLevels = desc.MipLevels;
                mdata.miscFlags = 0;
                mdata.miscFlags2 = 0;
                mdata.format = desc.Format;
                mdata.dimension = TEX_DIMENSION_TEXTURE1D;
                texMetadataCache = mdata;

                hr = scratchImageResult.Initialize(mdata);
                if (FAILED(hr))
                {
                    return hr;
                }
                break;
            }

            case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            {
                TexMetadata mdata{};
                mdata.width = static_cast<size_t>(desc.Width);
                mdata.height = desc.Height;
                mdata.depth = 1;
                mdata.arraySize = desc.DepthOrArraySize;
                mdata.mipLevels = desc.MipLevels;
                mdata.miscFlags = captureTextureDesc.isCubeMap ? TEX_MISC_TEXTURECUBE : 0u;
                mdata.miscFlags2 = 0;
                mdata.format = desc.Format;
                mdata.dimension = TEX_DIMENSION_TEXTURE2D;
                texMetadataCache = mdata;

                hr = scratchImageResult.Initialize(mdata);
                if (FAILED(hr))
                {
                    return hr;
                }
                break;
            }

            case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            {
                TexMetadata mdata{};
                mdata.width = static_cast<size_t>(desc.Width);
                mdata.height = desc.Height;
                mdata.depth = desc.DepthOrArraySize;
                mdata.arraySize = 1;
                mdata.mipLevels = desc.MipLevels;
                mdata.miscFlags = 0;
                mdata.miscFlags2 = 0;
                mdata.format = desc.Format;
                mdata.dimension = TEX_DIMENSION_TEXTURE3D;
                texMetadataCache = mdata;

                hr = scratchImageResult.Initialize(mdata);
                if (FAILED(hr))
                {
                    return hr;
                }
                break;
            }

            default:
            {
                return E_FAIL;
            }
        }

        BYTE* pData = nullptr;
        hr = captureTextureDesc.pStaging->Map(0, nullptr, reinterpret_cast<void **>(&pData));
        if (FAILED(hr))
        {
            scratchImageResult.Release();
            return E_FAIL;
        }

        const UINT arraySize = (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) ? 1u : desc.DepthOrArraySize;
        for (UINT plane = 0u; plane < captureTextureDesc.numberOfPlanes; ++plane)
        {
            for (UINT item = 0u; item < arraySize; ++item)
            {
                for (UINT level = 0u; level < desc.MipLevels; ++level)
                {
                    const UINT dindex = D3D12CalcSubresource(level, item, plane, desc.MipLevels, arraySize);
                    assert(dindex < captureTextureDesc.numberOfResources);

                    const Image* img = scratchImageResult.GetImage(level, item, 0);
                    if (!img)
                    {
                        captureTextureDesc.pStaging->Unmap(0, nullptr);
                        scratchImageResult.Release();
                        return E_FAIL;
                    }

                    if (!img->pixels)
                    {
                        captureTextureDesc.pStaging->Unmap(0, nullptr);
                        scratchImageResult.Release();
                        return E_POINTER;
                    }

                    D3D12_MEMCPY_DEST destData{ img->pixels, img->rowPitch, img->slicePitch };

                    AdjustPlaneResource<D3D12_MEMCPY_DEST, uintptr_t>(img->format, img->height, plane, destData);

                    D3D12_SUBRESOURCE_DATA srcData{
                                    pData + pLayout[dindex].Offset,
                                    static_cast<LONG_PTR>(pLayout[dindex].Footprint.RowPitch),
                                    static_cast<LONG_PTR>(pLayout[dindex].Footprint.RowPitch) * static_cast<LONG_PTR>(pNumRows[dindex]) };

                    if (pRowSizesInBytes[dindex] > static_cast<SIZE_T>(-1))
                    {
                        captureTextureDesc.pStaging->Unmap(0, nullptr);
                        scratchImageResult.Release();
                        return E_FAIL;
                    }

                    MemcpySubresource(&destData, &srcData,
                                        static_cast<SIZE_T>(pRowSizesInBytes[dindex]),
                                        pNumRows[dindex],
                                        pLayout[dindex].Footprint.Depth);
                }
            }
        }

        captureTextureDesc.pStaging->Unmap(0, nullptr);

        return S_OK;
    }

    _Use_decl_annotations_
    HRESULT SaveToDDSFileImmediately(_In_ const CaptureTextureDesc& captureTextureDesc, _In_ DDS_FLAGS flags, _In_z_ const wchar_t* szFile) noexcept
    {
        if (!captureTextureDesc.pStaging || !captureTextureDesc.layoutBuff)
        {
            return E_INVALIDARG;
        }

        // Mapping staging resource
        HRESULT result = S_OK;
        ScratchImage scratchImageResult{};
        TexMetadata texMetadataCache{};

        result = GetScratchImageFromCapturedTexture(captureTextureDesc, scratchImageResult, texMetadataCache);
        if (FAILED(result))
        {
            return result;
        }

        // Save to dds file
        result = SaveToDDSFile(scratchImageResult.GetImages(), captureTextureDesc.numberOfResources, texMetadataCache, flags, szFile);

        return result;
    }

    _Use_decl_annotations_
    HRESULT __cdecl CaptureBufferDeferred(
            _In_ ID3D12Device* device, _In_ ID3D12GraphicsCommandList* pCommandList, _In_ ID3D12Resource* pSource,
            CaptureTextureDesc &captureTextureDesc,
            _In_ D3D12_RESOURCE_STATES beforeState,
            _In_ D3D12_RESOURCE_STATES afterState) noexcept
    {
        using Microsoft::WRL::ComPtr;

        HRESULT result = S_OK;

        if (!device || !pCommandList || !pSource)
            return E_INVALIDARG;

#if defined(_MSC_VER) || !defined(_WIN32)
        auto const desc = pSource->GetDesc();
#else
        D3D12_RESOURCE_DESC tmpDesc;
        auto const& desc = *pSource->GetDesc(&tmpDesc);
#endif
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
            return E_INVALIDARG;

        captureTextureDesc.numberOfResources = 1;
        captureTextureDesc.numberOfPlanes = D3D12GetFormatPlaneCount(device, desc.Format);
        if (!captureTextureDesc.numberOfPlanes)
            return E_INVALIDARG;
        captureTextureDesc.isCubeMap = false;
        captureTextureDesc.layoutBuff = nullptr;
        captureTextureDesc.capturedResourceDesc = desc;

        D3D12_HEAP_PROPERTIES sourceHeapProperties{};
        result = pSource->GetHeapProperties(&sourceHeapProperties, nullptr);
        if (SUCCEEDED(result) && sourceHeapProperties.Type == D3D12_HEAP_TYPE_READBACK)
        {
            // Handle case where the source is already a staging texture we can use directly
            captureTextureDesc.pStaging = pSource;
            pSource->AddRef();
            return S_OK;
        }

        D3D12_RESOURCE_DESC staging_resource_desc{};
        staging_resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        staging_resource_desc.Alignment = 0;
        staging_resource_desc.Width = desc.Width;
        staging_resource_desc.Height = 1;
        staging_resource_desc.DepthOrArraySize = 1;
        staging_resource_desc.MipLevels = 1;
        staging_resource_desc.Format = DXGI_FORMAT_UNKNOWN;
        staging_resource_desc.SampleDesc.Count = 1;
        staging_resource_desc.SampleDesc.Quality = 0;
        staging_resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        staging_resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES staging_heap_props{};
        staging_heap_props.Type = D3D12_HEAP_TYPE_READBACK;
        staging_heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        staging_heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        staging_heap_props.CreationNodeMask = 1;
        staging_heap_props.VisibleNodeMask = 1;

        // Create a staging texture
        result = device->CreateCommittedResource(
                &staging_heap_props,
                D3D12_HEAP_FLAG_NONE,
                &staging_resource_desc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_GRAPHICS_PPV_ARGS(captureTextureDesc.pStaging.GetAddressOf()));
        if (FAILED(result))
        {
            return result;
        }

        assert(captureTextureDesc.pStaging != nullptr);

        // Transition the resource if necessary
        TransitionResource(pCommandList, pSource, beforeState, D3D12_RESOURCE_STATE_COPY_SOURCE);

        // Copy resource
        pCommandList->CopyResource(captureTextureDesc.pStaging.Get(), pSource);

        // Transition the resource to the next state
        TransitionResource(pCommandList, pSource, D3D12_RESOURCE_STATE_COPY_SOURCE, afterState);

        return result;
    }

    _Use_decl_annotations_
    HRESULT __cdecl SaveToWICFileImmediately(_In_ const CaptureTextureDesc &captureTextureDesc, _In_ WIC_FLAGS flags, _In_ REFGUID guidContainerFormat,
                                        _In_z_ const wchar_t *szFile, _In_opt_ const GUID *targetFormat,
                                        _In_ std::function<void __cdecl(IPropertyBag2*)> setCustomProps) noexcept
    {
        if (!captureTextureDesc.pStaging || !captureTextureDesc.layoutBuff)
        {
            return E_INVALIDARG;
        }

        // Mapping staging resource
        HRESULT result = S_OK;
        ScratchImage scratchImageResult{};
        TexMetadata texMetadataCache{};

        result = GetScratchImageFromCapturedTexture(captureTextureDesc, scratchImageResult, texMetadataCache);
        if (FAILED(result))
        {
            return result;
        }

        // Save to wic-format file
        result = SaveToWICFile(scratchImageResult.GetImages(), captureTextureDesc.numberOfResources, flags, guidContainerFormat,
                                szFile, targetFormat, std::move(setCustomProps));
        return result;
    }

    _Use_decl_annotations_
    HRESULT __cdecl CaptureBufferImmediately(_In_ ID3D12CommandQueue *pCommandQueue,
                                            _In_ ID3D12Resource *pSource,
                                            _In_z_ const wchar_t *szFile,
                                            _In_ D3D12_RESOURCE_STATES beforeState,
                                            _In_ D3D12_RESOURCE_STATES afterState) noexcept
    {
        using Microsoft::WRL::ComPtr;

        HRESULT result = E_INVALIDARG;
        auto target_resource_desc = pSource->GetDesc();
        if (target_resource_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || target_resource_desc.Width > std::numeric_limits<uint32_t>::max())
        {
            return result;
        }

        // Get device
        ComPtr<ID3D12Device> device = nullptr;
        result = pCommandQueue->GetDevice(IID_GRAPHICS_PPV_ARGS(device.GetAddressOf()));
        if (FAILED(result))
        {
            return result;
        }

        // Create a command allocator
        ComPtr<ID3D12CommandAllocator> command_alloc = nullptr;
        result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_GRAPHICS_PPV_ARGS(command_alloc.GetAddressOf()));
        if (FAILED(result))
        {
            return result;
        }

        // Spin up a new command list
        ComPtr<ID3D12GraphicsCommandList> command_list = nullptr;
        result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_alloc.Get(), nullptr, IID_GRAPHICS_PPV_ARGS(command_list.GetAddressOf()));
        if (FAILED(result))
        {
            return result;
        }

        // Create a fence
        ComPtr<ID3D12Fence> fence = nullptr;
        result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_GRAPHICS_PPV_ARGS(fence.GetAddressOf()));
        if (FAILED(result))
        {
            return result;
        }

        D3D12_RESOURCE_DESC staging_resource_desc{};
        staging_resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        staging_resource_desc.Alignment = 0;
        staging_resource_desc.Width = target_resource_desc.Width;
        staging_resource_desc.Height = 1;
        staging_resource_desc.DepthOrArraySize = 1;
        staging_resource_desc.MipLevels = 1;
        staging_resource_desc.Format = DXGI_FORMAT_UNKNOWN;
        staging_resource_desc.SampleDesc.Count = 1;
        staging_resource_desc.SampleDesc.Quality = 0;
        staging_resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        staging_resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES staging_heap_props{};
        staging_heap_props.Type = D3D12_HEAP_TYPE_READBACK;
        staging_heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        staging_heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        staging_heap_props.CreationNodeMask = 1;
        staging_heap_props.VisibleNodeMask = 1;

        // Create a staging texture
        ComPtr<ID3D12Resource> staging_resource = nullptr;
        result = device->CreateCommittedResource(
                &staging_heap_props,
                D3D12_HEAP_FLAG_NONE,
                &staging_resource_desc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_GRAPHICS_PPV_ARGS(staging_resource.GetAddressOf()));
        if (FAILED(result))
        {
            return result;
        }

        // Transition the resource if necessary
        TransitionResource(command_list.Get(), pSource, beforeState, D3D12_RESOURCE_STATE_COPY_SOURCE);

        // Copy resource
        command_list->CopyResource(staging_resource.Get(), pSource);

        // Transition the resource to the next state
        TransitionResource(command_list.Get(), pSource, D3D12_RESOURCE_STATE_COPY_SOURCE, afterState);

        result = command_list->Close();
        if (FAILED(result))
        {
            return result;
        }

        // Execute the command list
        std::array<ID3D12CommandList *, 1> command_lists{ command_list.Get() };
        pCommandQueue->ExecuteCommandLists(static_cast<uint32_t>(command_lists.size()), command_lists.data());

        // Signal the fence
        result = pCommandQueue->Signal(fence.Get(), 1);
        if (FAILED(result))
        {
            return result;
        }

        // Block until the copy is complete
        while (fence->GetCompletedValue() < 1)
        {
#if defined(_WIN32)
            SwitchToThread();
#else
            std::this_thread::yield();
#endif
        }

        // Read back and write to file
        D3D12_RANGE read_range{ 0, static_cast<size_t>(staging_resource_desc.Width) };
        D3D12_RANGE write_range{ 0, 0 };
        void *mapped_data = nullptr;
        result = staging_resource->Map(0, &read_range, &mapped_data);
        if (FAILED(result))
        {
            return result;
        }

        std::ofstream output_file{ szFile, std::ios::binary };
        if (output_file.is_open())
        {
            output_file.write(reinterpret_cast<char *>(mapped_data), static_cast<std::streamsize>(staging_resource_desc.Width));
        }

        staging_resource->Unmap(0, &write_range);

        return S_OK;
    }

    _Use_decl_annotations_
    HRESULT SaveToBinFileImmediately(_In_ const CaptureTextureDesc& captureTextureDesc, _In_z_ const wchar_t* szFile) noexcept
    {
        if (!captureTextureDesc.pStaging)
        {
            return E_INVALIDARG;
        }

        HRESULT result = S_OK;
        auto stagingResourceDesc = captureTextureDesc.pStaging->GetDesc(); // Since staging resource may be copied from texture 2d, texture 2d array and ect.

        // Mapping resource, and write data to file
        D3D12_RANGE read_range{ 0, static_cast<size_t>(stagingResourceDesc.Width) };
        D3D12_RANGE write_range{ 0, 0 };
        void *mapped_data = nullptr;
        result = captureTextureDesc.pStaging->Map(0, &read_range, &mapped_data);
        if (FAILED(result))
        {
            return result;
        }

        std::ofstream output_file{ szFile, std::ios::binary };
        if (output_file.is_open())
        {
            output_file.write(static_cast<char *>(mapped_data), static_cast<std::streamsize>(stagingResourceDesc.Width));
        } else
        {
            result = E_ACCESSDENIED;
        }

        captureTextureDesc.pStaging->Unmap(0, &write_range);

        return result;
    }
}