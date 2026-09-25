#include "../h/RenderingSystem.h"
#include "../h/d3dUtil.h"
#include "../h/d3dx12.h"
#include "../h/ThrowIfFailed.h"
#include <DirectXMath.h>

RenderingSystem::RenderingSystem(
    ID3D12Device* device,
    ID3D12CommandQueue* commandQueue,
    ID3D12GraphicsCommandList* commandList,
    ID3D12CommandAllocator* commandAllocator,
    ID3D12Fence* fence,
    UINT swapChainBufferCount,
    DXGI_FORMAT backBufferFormat)
    : mDevice(device)
    , mCommandQueue(commandQueue)
    , mCommandList(commandList)
    , mCommandAllocator(commandAllocator)
    , mFence(fence)
    , mSwapChainBufferCount(swapChainBufferCount)
    , mBackBufferFormat(backBufferFormat)
{
    mRtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mDsvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    mCbvSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

RenderingSystem::~RenderingSystem()
{
    Shutdown();
}

bool RenderingSystem::Initialize(UINT width, UINT height)
{
    mWidth = width;
    mHeight = height;

    // Два независимых аллокатора команд (см. комментарий в заголовке)
    ThrowIfFailed(mDevice->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mGeometryAllocator)));
    ThrowIfFailed(mDevice->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mLightingAllocator)));

    if (!CreateGBuffer(width, height))
        return false;

    if (!CreateLightingResources())
        return false;

    return true;
}

bool RenderingSystem::CreateGBuffer(UINT width, UINT height)
{
    mGBuffer = std::make_unique<GBuffer>();
    return mGBuffer->Initialize(mDevice, width, height);
}

bool RenderingSystem::CreateLightingResources()
{
    auto vsLighting = d3dUtil::CompileShader(
        L"../src/lighting.hlsl",
        nullptr,
        "VS",
        "vs_5_0");

    if (!vsLighting)
    {
        OutputDebugStringA("Failed to compile lighting VS\n");
        return false;
    }

    auto psLighting = d3dUtil::CompileShader(
        L"../src/lighting.hlsl",
        nullptr,
        "PS",
        "ps_5_0");

    if (!psLighting)
    {
        OutputDebugStringA("Failed to compile lighting PS\n");
        return false;
    }

    // ============= ROOT SIGNATURE =============
    D3D12_DESCRIPTOR_RANGE srvRanges[4];

    srvRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRanges[0].NumDescriptors = 1;
    srvRanges[0].BaseShaderRegister = 0;
    srvRanges[0].RegisterSpace = 0;
    srvRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    srvRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRanges[1].NumDescriptors = 1;
    srvRanges[1].BaseShaderRegister = 1;
    srvRanges[1].RegisterSpace = 0;
    srvRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    srvRanges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRanges[2].NumDescriptors = 1;
    srvRanges[2].BaseShaderRegister = 2;
    srvRanges[2].RegisterSpace = 0;
    srvRanges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    srvRanges[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRanges[3].NumDescriptors = 1;
    srvRanges[3].BaseShaderRegister = 3;   // gSpecularMap
    srvRanges[3].RegisterSpace = 0;
    srvRanges[3].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[3] = {};

    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 4;
    rootParams[0].DescriptorTable.pDescriptorRanges = srvRanges;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].Descriptor.ShaderRegister = 0;
    rootParams[1].Descriptor.RegisterSpace = 0;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[2].Descriptor.ShaderRegister = 1;
    rootParams[2].Descriptor.RegisterSpace = 0;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    sampler.MinLOD = 0;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 3;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &sampler;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                             &serializedRootSig, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        OutputDebugStringA("Failed to serialize lighting root signature\n");
        return false;
    }

    hr = mDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
                                     serializedRootSig->GetBufferSize(),
                                     IID_PPV_ARGS(&mLightingRootSignature));
    if (FAILED(hr))
    {
        OutputDebugStringA("Failed to create lighting root signature\n");
        return false;
    }

    // ============= PSO =============
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

    psoDesc.VS = { vsLighting->GetBufferPointer(), vsLighting->GetBufferSize() };
    psoDesc.PS = { psLighting->GetBufferPointer(), psLighting->GetBufferSize() };
    psoDesc.pRootSignature = mLightingRootSignature.Get();
    psoDesc.InputLayout = { nullptr, 0 };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mBackBufferFormat;
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.SampleMask = UINT_MAX;

    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    hr = mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mLightingPSO));
    if (FAILED(hr))
    {
        OutputDebugStringA("Failed to create lighting PSO\n");
        return false;
    }

    mLightingCB = std::make_unique<UploadBuffer<LightConstants>>(
        mDevice,
        200,
        true);

    return true;
}

void RenderingSystem::GeometryPass(
    ID3D12PipelineState* pso,
    ID3D12RootSignature* rootSignature,
    ID3D12DescriptorHeap* cbvSrvHeap,
    UINT cbvSrvDescriptorSize,
    const std::vector<Submesh>& submeshes,
    const std::vector<Material>& materials,
    ID3D12Resource* vertexBuffer,
    ID3D12Resource* indexBuffer,
    const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
    const D3D12_INDEX_BUFFER_VIEW& indexBufferView,
    ID3D12Resource* depthStencilBuffer,
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
    const D3D12_VIEWPORT& viewport,
    const D3D12_RECT& scissorRect,
    UINT materialCount)
{
    if (!mGBuffer) return;

    mGeometryAllocator->Reset();
    mCommandList->Reset(mGeometryAllocator.Get(), pso);

    // Переводим G-буфер текстуры в состояние RENDER_TARGET
    D3D12_RESOURCE_BARRIER barriers[GBuffer::GBUFFER_COUNT];
    for (int i = 0; i < GBuffer::GBUFFER_COUNT; ++i)
    {
        barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
            mGBuffer->GetTexture((GBuffer::GBUFFER_TEXTURE_TYPE)i),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    mCommandList->ResourceBarrier(GBuffer::GBUFFER_COUNT, barriers);

    // depth-буфер уже создан в состоянии DEPTH_WRITE (см. CreateDepthStencilBuffer)
    mGBuffer->ClearRenderTargets(mCommandList);
    mCommandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    mGBuffer->SetRenderTargets(mCommandList, dsvHandle);

    mCommandList->RSSetViewports(1, &viewport);
    mCommandList->RSSetScissorRects(1, &scissorRect);
    mCommandList->SetGraphicsRootSignature(rootSignature);
    ID3D12DescriptorHeap* heaps[] = { cbvSrvHeap };
    mCommandList->SetDescriptorHeaps(1, heaps);

    // CBV (b0)
    mCommandList->SetGraphicsRootDescriptorTable(0, cbvSrvHeap->GetGPUDescriptorHandleForHeapStart());

    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->IASetVertexBuffers(0, 1, &vertexBufferView);
    mCommandList->IASetIndexBuffer(&indexBufferView);

    for (auto& sm : submeshes)
    {
        const Material* mat = nullptr;
        for (auto& m : materials)
        {
            if (m.Name == sm.MaterialName)
            {
                mat = &m;
                break;
            }
        }

        if (!mat) continue;

        // SRV диффузной текстуры материала (t0)
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle =
            cbvSrvHeap->GetGPUDescriptorHandleForHeapStart();
        srvHandle.ptr += (1 + mat->SrvHeapIndex) * cbvSrvDescriptorSize;
        mCommandList->SetGraphicsRootDescriptorTable(1, srvHandle);

        // Параметры блика материала (root parameter 2, cbuffer b2)
        const float specularConstants[4] =
        {
            mat->SpecularColor.x,
            mat->SpecularColor.y,
            mat->SpecularColor.z,
            mat->SpecularPower
        };
        mCommandList->SetGraphicsRoot32BitConstants(2, 4, specularConstants, 0);

        mCommandList->DrawIndexedInstanced(sm.IndexCount, 1, sm.IndexStart, 0, 0);
    }

    // Переводим G-буфер обратно
    for (int i = 0; i < GBuffer::GBUFFER_COUNT; ++i)
    {
        barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
            mGBuffer->GetTexture((GBuffer::GBUFFER_TEXTURE_TYPE)i),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    mCommandList->ResourceBarrier(GBuffer::GBUFFER_COUNT, barriers);

    mCommandList->Close();

    ID3D12CommandList* cmdLists[] = { mCommandList };
    mCommandQueue->ExecuteCommandLists(1, cmdLists);
}

void RenderingSystem::LightingPass(
    ID3D12Resource* backBuffer,
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
    const std::vector<Light>& lights,
    const DirectX::XMFLOAT3& cameraPos,
    const D3D12_VIEWPORT& viewport,
    const D3D12_RECT& scissorRect,
    int& currBackBufferIndex,
    IDXGISwapChain* swapChain,
    ID3D12PipelineState* lightingPSO,
    ID3D12RootSignature* lightingRootSignature,
    UploadBuffer<LightConstants>* lightingCB,
    UploadBuffer<CameraConstants>* cameraCB,
    GBuffer* gBuffer,
    int debugMode,
    ID3D12PipelineState* orbPSO,
    ID3D12RootSignature* orbRootSignature,
    const std::vector<FlyingBulb>* bulbs,
    const DirectX::XMFLOAT4X4* viewProj,
    const DirectX::XMFLOAT3& cameraRight,
    const DirectX::XMFLOAT3& cameraUp)
{
    mLightingAllocator->Reset();
    mCommandList->Reset(mLightingAllocator.Get(), lightingPSO);

    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer,
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &barrier);

    mCommandList->RSSetViewports(1, &viewport);
    mCommandList->RSSetScissorRects(1, &scissorRect);

    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    mCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    mCommandList->OMSetRenderTargets(1, &rtvHandle, true, nullptr);

    mCommandList->SetGraphicsRootSignature(lightingRootSignature);
    ID3D12DescriptorHeap* heaps[] = { gBuffer->mSrvHeap.Get() };
    mCommandList->SetDescriptorHeaps(1, heaps);
    mCommandList->SetGraphicsRootDescriptorTable(0, gBuffer->mSrvHeap->GetGPUDescriptorHandleForHeapStart());

    if (cameraCB)
    {
        D3D12_GPU_VIRTUAL_ADDRESS cameraAddr = cameraCB->Resource()->GetGPUVirtualAddress();
        mCommandList->SetGraphicsRootConstantBufferView(2, cameraAddr);
    }

    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->IASetVertexBuffers(0, 0, nullptr);
    mCommandList->IASetIndexBuffer(nullptr);

    if (lightingCB && debugMode != 0)
    {
        // Отладочный вывод слоя G-буфера: один полноэкранный проход
        D3D12_GPU_VIRTUAL_ADDRESS debugAddr = lightingCB->Resource()->GetGPUVirtualAddress();
        mCommandList->SetGraphicsRootConstantBufferView(1, debugAddr);

        mCommandList->DrawInstanced(3, 1, 0, 0);
    }
    else if (lightingCB)
    {
        D3D12_GPU_VIRTUAL_ADDRESS baseAddr = lightingCB->Resource()->GetGPUVirtualAddress();
        UINT elementSize = lightingCB->GetElementSize();

        for (size_t i = 0; i < lights.size(); ++i)
        {
            LightConstants lightConstants;
            lightConstants.SetFromLight(lights[i], cameraPos);
            lightingCB->CopyData((UINT)i, lightConstants);

            D3D12_GPU_VIRTUAL_ADDRESS cbAddr = baseAddr + i * elementSize;
            mCommandList->SetGraphicsRootConstantBufferView(1, cbAddr);

            mCommandList->DrawInstanced(3, 1, 0, 0);
        }
    }


    // ===== ЛЕТЯЩИЕ ЛАМПОЧКИ =====
    // Рисуются после источников света: каждый — маленький густой комок света
    // (аддитивный билборд, BLEND_ONE/ONE). Свой корневой подписи хватает одного
    // root-constant блока: матрица вида-проекции, базис камеры и параметры шара.
    if (bulbs && !bulbs->empty() && orbPSO && orbRootSignature && viewProj)
    {
        mCommandList->SetPipelineState(orbPSO);
        mCommandList->SetGraphicsRootSignature(orbRootSignature);

        const float* vp = &viewProj->m[0][0];

        for (const auto& bulb : *bulbs)
        {
            float constants[32] = {};

            for (int i = 0; i < 16; ++i)
                constants[i] = vp[i];

            constants[16] = cameraRight.x;
            constants[17] = cameraRight.y;
            constants[18] = cameraRight.z;
            constants[19] = BulbSettings::Radius;

            constants[20] = cameraUp.x;
            constants[21] = cameraUp.y;
            constants[22] = cameraUp.z;
            constants[23] = BulbSettings::CoreIntensity;

            constants[24] = bulb.Position.x;
            constants[25] = bulb.Position.y;
            constants[26] = bulb.Position.z;
            constants[27] = 0.0f;

            constants[28] = BulbSettings::Color[0];
            constants[29] = BulbSettings::Color[1];
            constants[30] = BulbSettings::Color[2];
            constants[31] = 0.0f;

            mCommandList->SetGraphicsRoot32BitConstants(0, 32, constants, 0);
            mCommandList->DrawInstanced(6, 1, 0, 0);
        }
    }

    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    mCommandList->ResourceBarrier(1, &barrier);

    mCommandList->Close();

    ID3D12CommandList* cmdLists[] = { mCommandList };
    mCommandQueue->ExecuteCommandLists(1, cmdLists);

    swapChain->Present(0, 0);
    currBackBufferIndex = (currBackBufferIndex + 1) % mSwapChainBufferCount;
}

void RenderingSystem::Shutdown()
{
    FlushCommandQueue();

    if (mGBuffer)
    {
        mGBuffer->Shutdown();
        mGBuffer.reset();
    }

    mLightingPSO.Reset();
    mLightingRootSignature.Reset();
    mLightingCB.reset();
    mGeometryAllocator.Reset();
    mLightingAllocator.Reset();
}

void RenderingSystem::FlushCommandQueue()
{
    static UINT64 fenceValue = 1;

    mCommandQueue->Signal(mFence, fenceValue);

    if (mFence->GetCompletedValue() < fenceValue)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        mFence->SetEventOnCompletion(fenceValue, eventHandle);
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    fenceValue++;
}