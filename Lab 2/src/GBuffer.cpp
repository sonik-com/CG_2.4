#include "../h/GBuffer.h"
#include "../h/ThrowIfFailed.h"
#include <DirectXMath.h>

namespace
{
    // Значения очистки для каждого слоя G-буфера
    const float kClearValues[GBuffer::GBUFFER_COUNT][4] =
    {
        { 0.0f, 0.0f, 0.0f, 1.0f },  // Albedo
        { 0.0f, 0.0f, 0.0f, 0.0f },  // Normal
        { 1.0f, 0.0f, 0.0f, 0.0f },  // Depth = 1.0 (дальняя плоскость)
        { 0.0f, 0.0f, 0.0f, 0.0f }   // Specular
    };
}

DXGI_FORMAT GBuffer::GetFormat(GBUFFER_TEXTURE_TYPE type)
{
    switch (type)
    {
    case GBUFFER_ALBEDO:   return DXGI_FORMAT_R8G8B8A8_UNORM;
    case GBUFFER_NORMAL:   return DXGI_FORMAT_R16G16B16A16_FLOAT;  // выше точность нормалей
    case GBUFFER_DEPTH:    return DXGI_FORMAT_R32_FLOAT;
    case GBUFFER_SPECULAR: return DXGI_FORMAT_R8G8B8A8_UNORM;
    default:               return DXGI_FORMAT_UNKNOWN;
    }
}

bool GBuffer::Initialize(ID3D12Device* device, UINT width, UINT height)
{
    mWidth = width;
    mHeight = height;

    // Получаем размеры дескрипторов
    mRtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mCbvSrvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Создаем текстуры
    if (!CreateTextures(device))
        return false;

    // Создаем RTV кучу и представления
    if (!CreateRTVs(device))
        return false;

    // Создаем SRV кучу и представления
    if (!CreateSRVs(device))
        return false;

    return true;
}

bool GBuffer::CreateTextures(ID3D12Device* device)
{
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = mWidth;
    texDesc.Height = mHeight;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    for (int i = 0; i < GBUFFER_COUNT; ++i)
    {
        texDesc.Format = GetFormat((GBUFFER_TEXTURE_TYPE)i);

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = texDesc.Format;
        for (int c = 0; c < 4; ++c)
            clearValue.Color[c] = kClearValues[i][c];

        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,  // в шейдере читается в light pass
            &clearValue,
            IID_PPV_ARGS(&mTextures[i])
        ));
    }

    return true;
}

bool GBuffer::CreateRTVs(ID3D12Device* device)
{
    // Создаем RTV кучу
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = GBUFFER_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ThrowIfFailed(device->CreateDescriptorHeap(
        &rtvHeapDesc,
        IID_PPV_ARGS(&mRtvHeap)
    ));

    // Создаем RTV для каждой текстуры
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (int i = 0; i < GBUFFER_COUNT; ++i)
    {
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = GetFormat((GBUFFER_TEXTURE_TYPE)i);
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        device->CreateRenderTargetView(mTextures[i].Get(), &rtvDesc, rtvHandle);

        rtvHandle.ptr += mRtvDescriptorSize;
    }

    return true;
}

bool GBuffer::CreateSRVs(ID3D12Device* device)
{
    // Создаем SRV кучу
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = GBUFFER_COUNT;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ThrowIfFailed(device->CreateDescriptorHeap(
        &srvHeapDesc,
        IID_PPV_ARGS(&mSrvHeap)
    ));

    // Создаем SRV для каждой текстуры
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = mSrvHeap->GetCPUDescriptorHandleForHeapStart();

    for (int i = 0; i < GBUFFER_COUNT; ++i)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = GetFormat((GBUFFER_TEXTURE_TYPE)i);
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        device->CreateShaderResourceView(mTextures[i].Get(), &srvDesc, srvHandle);

        srvHandle.ptr += mCbvSrvDescriptorSize;
    }

    return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::GetRTV(GBUFFER_TEXTURE_TYPE type) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += type * mRtvDescriptorSize;
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::GetSRV(GBUFFER_TEXTURE_TYPE type) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = mSrvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += type * mCbvSrvDescriptorSize;
    return handle;
}

void GBuffer::ClearRenderTargets(ID3D12GraphicsCommandList* cmdList)
{
    for (int i = 0; i < GBUFFER_COUNT; ++i)
    {
        cmdList->ClearRenderTargetView(
            GetRTV((GBUFFER_TEXTURE_TYPE)i),
            kClearValues[i],
            0,
            nullptr);
    }
}

void GBuffer::SetRenderTargets(ID3D12GraphicsCommandList* cmdList,
                               D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[GBUFFER_COUNT];
    for (int i = 0; i < GBUFFER_COUNT; ++i)
        rtvHandles[i] = GetRTV((GBUFFER_TEXTURE_TYPE)i);

    cmdList->OMSetRenderTargets(GBUFFER_COUNT, rtvHandles, false, &dsvHandle);
}

void GBuffer::Shutdown()
{
    for (int i = 0; i < GBUFFER_COUNT; ++i)
    {
        mTextures[i].Reset();
    }
    mRtvHeap.Reset();
    mSrvHeap.Reset();
}
