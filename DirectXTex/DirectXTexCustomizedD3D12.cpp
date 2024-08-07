//
// Created by ZZK on 2024/8/7.
//

#include "DirectXTexCustomized.h"

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
    template<typename T, typename PT> void AdjustPlaneResource(
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

        if (stateBefore == stateAfter)
            return;

        D3D12_RESOURCE_BARRIER desc = {};
        desc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        desc.Transition.pResource = resource;
        desc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        desc.Transition.StateBefore = stateBefore;
        desc.Transition.StateAfter = stateAfter;

        commandList->ResourceBarrier(1, &desc);
    }

    _Use_decl_annotations_
    HRESULT __cdecl CaptureTextureDeferred(
            _In_ ID3D12Device* device, _In_ ID3D12GraphicsCommandList* pCommandList, _In_ ID3D12Resource* pSource,
            CaptureTextureDesc &captureTextureDesc, _In_ bool isCubeMap,
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

        if ((captureTextureDesc.numberOfPlanes > 1) && IsDepthStencil(desc.Format))
        {
            // DirectX 12 uses planes for stencil, DirectX 11 does not
            return HRESULT_E_NOT_SUPPORTED;
        }

        captureTextureDesc.numberOfResources = (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
                            ? 1u : desc.DepthOrArraySize;
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
        D3D12_RESOURCE_DESC bufferDesc = {};
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
    HRESULT SaveToDDSFileImmediately(_In_ const CaptureTextureDesc& captureTextureDesc, _In_ DDS_FLAGS flags, _In_z_ const wchar_t* szFile) noexcept
    {
        if (!captureTextureDesc.pStaging || !captureTextureDesc.layoutBuff)
            return E_INVALIDARG;

        // Mapping staging resource
        HRESULT hr = S_OK;
        auto pLayout = reinterpret_cast<const D3D12_PLACED_SUBRESOURCE_FOOTPRINT *>(captureTextureDesc.layoutBuff.get());
        auto pRowSizesInBytes = reinterpret_cast<const UINT64 *>(pLayout + captureTextureDesc.numberOfResources);
        auto pNumRows = reinterpret_cast<const UINT *>(pRowSizesInBytes + captureTextureDesc.numberOfResources);
        auto desc = captureTextureDesc.capturedResourceDesc;
        ScratchImage result{};
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

                hr = result.Initialize(mdata);
                if (FAILED(hr))
                    return hr;
            }
                break;

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

                hr = result.Initialize(mdata);
                if (FAILED(hr))
                    return hr;
            }
                break;

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

                hr = result.Initialize(mdata);
                if (FAILED(hr))
                    return hr;
            }
                break;

            default:
                return E_FAIL;
        }

        BYTE* pData = nullptr;
        hr = captureTextureDesc.pStaging->Map(0, nullptr, reinterpret_cast<void **>(&pData));
        if (FAILED(hr))
        {
            result.Release();
            return E_FAIL;
        }

        const UINT arraySize = (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
                                ? 1u
                                : desc.DepthOrArraySize;

        for (UINT plane = 0; plane < captureTextureDesc.numberOfPlanes; ++plane)
        {
            for (UINT item = 0; item < arraySize; ++item)
            {
                for (UINT level = 0; level < desc.MipLevels; ++level)
                {
                    const UINT dindex = D3D12CalcSubresource(level, item, plane, desc.MipLevels, arraySize);
                    assert(dindex < captureTextureDesc.numberOfResources);

                    const Image* img = result.GetImage(level, item, 0);
                    if (!img)
                    {
                        captureTextureDesc.pStaging->Unmap(0, nullptr);
                        result.Release();
                        return E_FAIL;
                    }

                    if (!img->pixels)
                    {
                        captureTextureDesc.pStaging->Unmap(0, nullptr);
                        result.Release();
                        return E_POINTER;
                    }

                    D3D12_MEMCPY_DEST destData = { img->pixels, img->rowPitch, img->slicePitch };

                    AdjustPlaneResource<D3D12_MEMCPY_DEST, uintptr_t>(img->format, img->height, plane, destData);

                    D3D12_SUBRESOURCE_DATA srcData =
                            {
                                    pData + pLayout[dindex].Offset,
                                    static_cast<LONG_PTR>(pLayout[dindex].Footprint.RowPitch),
                                    static_cast<LONG_PTR>(pLayout[dindex].Footprint.RowPitch) * static_cast<LONG_PTR>(pNumRows[dindex])
                            };

                    if (pRowSizesInBytes[dindex] > SIZE_T(-1))
                    {
                        captureTextureDesc.pStaging->Unmap(0, nullptr);
                        result.Release();
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

        // Save to dds file
        TexMetadata mdata = {};
        auto *image = result.GetImages();
        mdata.width = image->width;
        mdata.height = image->height;
        mdata.depth = 1;
        mdata.arraySize = 1;
        mdata.mipLevels = 1;
        mdata.format = image->format;
        mdata.dimension = TEX_DIMENSION_TEXTURE2D;

        SaveToDDSFile(image, 1, mdata, flags, szFile);

        return S_OK;
    }
}