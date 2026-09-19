#pragma once

#include <string>
#include <wrl/client.h>
#include <d3d12.h>
#include <DirectXMath.h>

// Материал из sponza.mtl.
// Структура взята из лабораторной 1: на материал приходится ровно одна
// диффузная текстура (второй текстуры и её смешивания здесь больше нет).
struct Material
{
    std::string Name;

    std::string DiffuseMap;               // Диффузная текстура (.tga)

    UINT SrvHeapIndex = 0;                // Индекс SRV для текстуры в куче

    Microsoft::WRL::ComPtr<ID3D12Resource> DiffuseTexture;

    // Параметры блика (модель Фонга). В используемом sponza.mtl у всех
    // материалов Ks = 0 и Ns = 7.84, поэтому задаются собственные значения,
    // иначе блик был бы полностью чёрным.
    DirectX::XMFLOAT3 SpecularColor = { 0.25f, 0.25f, 0.25f };
    float SpecularPower = 32.0f;
};
