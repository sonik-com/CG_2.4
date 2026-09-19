#pragma once

#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class GBuffer
{
public:
    enum GBUFFER_TEXTURE_TYPE
    {
        GBUFFER_ALBEDO = 0,   // RGB - диффузный цвет, A - не используется
        GBUFFER_NORMAL,       // RGB - нормаль в мировой системе координат
        GBUFFER_DEPTH,        // R   - глубина в NDC
        GBUFFER_SPECULAR,     // RGB - цвет блика (Ks), A - степень (power / 255)
        GBUFFER_COUNT
    };

    GBuffer() = default;
    ~GBuffer() = default;

    bool Initialize(ID3D12Device* device, UINT width, UINT height);
    void Shutdown();

    // Формат текстуры для каждого слоя G-буфера
    static DXGI_FORMAT GetFormat(GBUFFER_TEXTURE_TYPE type);

    // Геттеры для ресурсов
    ID3D12Resource* GetTexture(GBUFFER_TEXTURE_TYPE type) const { return mTextures[type].Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(GBUFFER_TEXTURE_TYPE type) const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetSRV(GBUFFER_TEXTURE_TYPE type) const;

    // Получить размеры
    UINT GetWidth() const { return mWidth; }
    UINT GetHeight() const { return mHeight; }

    // Очистка всех текстур G-буфера
    void ClearRenderTargets(ID3D12GraphicsCommandList* cmdList);

    // Установка всех слоёв G-буфера в качестве render targets
    void SetRenderTargets(ID3D12GraphicsCommandList* cmdList,
                          D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle);

    // Дескрипторные кучи
    ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    ComPtr<ID3D12DescriptorHeap> mSrvHeap;

private:
    bool CreateTextures(ID3D12Device* device);
    bool CreateRTVs(ID3D12Device* device);
    bool CreateSRVs(ID3D12Device* device);

    ComPtr<ID3D12Resource> mTextures[GBUFFER_COUNT];

    UINT mRtvDescriptorSize = 0;
    UINT mCbvSrvDescriptorSize = 0;
    UINT mWidth = 0;
    UINT mHeight = 0;
};
