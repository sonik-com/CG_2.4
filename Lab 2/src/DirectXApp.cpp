#include "../h/DirectXApp.h"
#include <DirectXMath.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <string>
#include <cfloat>
#include "../h/ThrowIfFailed.h"
#include "../h/Parser.h"
#include "../h/TgaLoader.h"
#include "../h/d3dUtil.h"
#include "../h/GBuffer.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;


DirectXApp::DirectXApp(Window& window) : window(window)
{
    XMStoreFloat4x4(&mWorld, XMMatrixIdentity());
    XMStoreFloat4x4(&mView, XMMatrixIdentity());
    XMStoreFloat4x4(&mProj, XMMatrixIdentity());
    mRenderingSystem = nullptr;
}

DirectXApp::~DirectXApp() {
    Shutdown();
}

// =========== Mouse Methods ==========
void DirectXApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(window.GetHwnd());
}

void DirectXApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void DirectXApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if (btnState & MK_RBUTTON)
    {
        float sensitivity = 0.005f;

        float dx = (x - mLastMousePos.x) * sensitivity;
        float dy = (y - mLastMousePos.y) * sensitivity;

        mYaw += dx;
        mPitch += dy;

        if (mPitch > XM_PIDIV2 - 0.1f)
            mPitch = XM_PIDIV2 - 0.1f;

        if (mPitch < -XM_PIDIV2 + 0.1f)
            mPitch = -XM_PIDIV2 + 0.1f;
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

// =========== Input Layout ===========
void DirectXApp::BuildInputLayout()
{
    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}

// =========== Shader ===========
void DirectXApp::BuildShaders()
{
    mvsByteCode = d3dUtil::CompileShader(
        L"../src/shaders.hlsl",
        nullptr,
        "VS",
        "vs_5_0"
    );

    mpsByteCode = d3dUtil::CompileShader(
        L"../src/shaders.hlsl",
        nullptr,
        "PS",
        "ps_5_0"
    );
}

// =========== CBV ===========
void DirectXApp::BuildConstantBuffer()
{
    mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(
        device.Get(),
        1,
        true
    );

    ObjectConstants objConstants;
    XMMATRIX view = XMMatrixIdentity();
    XMMATRIX proj = XMMatrixOrthographicLH(10.0f, 10.0f, 0.1f, 100.0f);
    XMMATRIX viewProj = view * proj;
    XMStoreFloat4x4(&objConstants.mWorldViewProj, XMMatrixTranspose(viewProj));

    objConstants.mUVTransform = XMFLOAT4(1.0f, 1.0f, 0.0f, 0.0f);

    mObjectCB->CopyData(0, objConstants);

    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
    cbvDesc.BufferLocation = cbAddress;
    cbvDesc.SizeInBytes = objCBByteSize;

    D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle = mCbvHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateConstantBufferView(&cbvDesc, cbvHandle);
}

// =========== Root Signature ===========
void DirectXApp::BuildRootSignature()
{
    // CBV range (b0)
    D3D12_DESCRIPTOR_RANGE cbvRange = {};
    cbvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    cbvRange.NumDescriptors = 1;
    cbvRange.BaseShaderRegister = 0;
    cbvRange.RegisterSpace = 0;
    cbvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // SRV range диффузной текстуры (t0)
    D3D12_DESCRIPTOR_RANGE srvRange1 = {};
    srvRange1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange1.NumDescriptors = 1;
    srvRange1.BaseShaderRegister = 0;
    srvRange1.RegisterSpace = 0;
    srvRange1.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[3];

    // Slot 0 → CBV
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[0].DescriptorTable.pDescriptorRanges = &cbvRange;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Slot 1 → SRV for texture1 (t0)
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &srvRange1;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Slot 2 → параметры блика материала (root constants, b2)
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[2].Constants.ShaderRegister = 2;
    rootParameters[2].Constants.RegisterSpace = 0;
    rootParameters[2].Constants.Num32BitValues = 4;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Static Sampler (s0)
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 3;
    rootSigDesc.pParameters = rootParameters;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &sampler;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;

    HRESULT hr = D3D12SerializeRootSignature(
        &rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(),
        errorBlob.GetAddressOf());

    if (FAILED(hr)) {
        MessageBoxA(NULL, "Failed to serialize root signature", "Error", MB_OK);
        return;
    }

    hr = device->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&mRootSignature));

    if (FAILED(hr)) {
        MessageBoxA(NULL, "Failed to create root signature", "Error", MB_OK);
    }
}

// =========== PSO (Pipeline State Object) ===========
void DirectXApp::BuildPSO()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
    ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

    psoDesc.VS = {
        reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()),
        mvsByteCode->GetBufferSize()
    };
    psoDesc.PS = {
        reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()),
        mpsByteCode->GetBufferSize()
    };

    psoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    psoDesc.pRootSignature = mRootSignature.Get();

    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    auto& rtBlend = blendDesc.RenderTarget[0];
    rtBlend.BlendEnable = FALSE;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState = blendDesc;

    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    // ЧЕТЫРЕ render target'а для G-буфера
    psoDesc.NumRenderTargets = 4;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;      // Albedo
    psoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT; // Normal
    psoDesc.RTVFormats[2] = DXGI_FORMAT_R32_FLOAT;          // Depth
    psoDesc.RTVFormats[3] = DXGI_FORMAT_R8G8B8A8_UNORM;     // Specular

    psoDesc.DSVFormat = mDepthStencilFormat;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO));
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create PSO", L"Error", MB_OK);
        return;
    }
}

// =========== BuildWireframePSO ===========
void DirectXApp::BuildWireframePSO()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC wireframePsoDesc;
    ZeroMemory(&wireframePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

    wireframePsoDesc.VS = {
        reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()),
        mvsByteCode->GetBufferSize()
    };
    wireframePsoDesc.PS = {
        reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()),
        mpsByteCode->GetBufferSize()
    };

    wireframePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    wireframePsoDesc.pRootSignature = mRootSignature.Get();

    wireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    wireframePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    wireframePsoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    wireframePsoDesc.RasterizerState.DepthClipEnable = TRUE;

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    auto& rtBlend = blendDesc.RenderTarget[0];
    rtBlend.BlendEnable = FALSE;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    wireframePsoDesc.BlendState = blendDesc;

    wireframePsoDesc.DepthStencilState.DepthEnable = TRUE;
    wireframePsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    wireframePsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    wireframePsoDesc.DepthStencilState.StencilEnable = FALSE;

    wireframePsoDesc.SampleMask = UINT_MAX;
    wireframePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    wireframePsoDesc.NumRenderTargets = 4;
    wireframePsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    wireframePsoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    wireframePsoDesc.RTVFormats[2] = DXGI_FORMAT_R32_FLOAT;
    wireframePsoDesc.RTVFormats[3] = DXGI_FORMAT_R8G8B8A8_UNORM;
    wireframePsoDesc.DSVFormat = mDepthStencilFormat;
    wireframePsoDesc.SampleDesc.Count = 1;
    wireframePsoDesc.SampleDesc.Quality = 0;

    HRESULT hr = device->CreateGraphicsPipelineState(&wireframePsoDesc, IID_PPV_ARGS(&mWireframePSO));
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create Wireframe PSO", L"Error", MB_OK);
        return;
    }
}

// =========== BuildObj ===========
void DirectXApp::BuildObj(const std::string& path)
{
    mSubmeshes.clear();

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    if (!LoadOBJ(path, vertices, indices, mSubmeshes))
    {
        MessageBoxA(nullptr, "Failed to load OBJ", "Error", MB_OK);
        return;
    }

    mIndexCount = static_cast<UINT>(indices.size());

    // Копия геометрии для проверки касания лампочек с моделью (см. RaycastSceneDistance).
    BuildCollisionData(vertices, indices);

    UINT vbByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex));
    UINT ibByteSize = static_cast<UINT>(indices.size() * sizeof(uint32_t));

    // VERTEX BUFFER
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC vbDesc = {};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Width = vbByteSize;
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mVertexBufferGPU)));

    void* mappedData = nullptr;
    mVertexBufferGPU->Map(0, nullptr, &mappedData);
    memcpy(mappedData, vertices.data(), vbByteSize);
    mVertexBufferGPU->Unmap(0, nullptr);

    mVertexBufferView.BufferLocation = mVertexBufferGPU->GetGPUVirtualAddress();
    mVertexBufferView.StrideInBytes = sizeof(Vertex);
    mVertexBufferView.SizeInBytes = vbByteSize;

    // INDEX BUFFER
    D3D12_RESOURCE_DESC ibDesc = {};
    ibDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    ibDesc.Width = ibByteSize;
    ibDesc.Height = 1;
    ibDesc.DepthOrArraySize = 1;
    ibDesc.MipLevels = 1;
    ibDesc.SampleDesc.Count = 1;
    ibDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &ibDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mIndexBufferGPU)));

    mIndexBufferGPU->Map(0, nullptr, &mappedData);
    memcpy(mappedData, indices.data(), ibByteSize);
    mIndexBufferGPU->Unmap(0, nullptr);

    mIndexBufferView.BufferLocation = mIndexBufferGPU->GetGPUVirtualAddress();
    mIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
    mIndexBufferView.SizeInBytes = ibByteSize;
}

// =========== Лампочки: ресурсы отрисовки ===========
// Лампочка рисуется аддитивным билбордом, поэтому нужна своя корневая подпись
// (один блок root-констант) и своё состояние конвейера.
void DirectXApp::BuildOrbResources()
{
    mvsOrbByteCode = d3dUtil::CompileShader(L"../src/orbs.hlsl", nullptr, "VSOrb", "vs_5_0");
    mpsOrbByteCode = d3dUtil::CompileShader(L"../src/orbs.hlsl", nullptr, "PSOrb", "ps_5_0");

    if (!mvsOrbByteCode || !mpsOrbByteCode)
    {
        MessageBoxA(nullptr, "Failed to compile orbs.hlsl", "Error", MB_OK);
        return;
    }

    // 32 root-константы: транспонированная вида-проекция (16), базис камеры и
    // параметры шара (16). Константного буфера и дескрипторов не требуется.
    D3D12_ROOT_PARAMETER rootParameter = {};
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameter.Constants.ShaderRegister = 0;
    rootParameter.Constants.RegisterSpace = 0;
    rootParameter.Constants.Num32BitValues = 32;
    rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 1;
    rootSigDesc.pParameters = &rootParameter;
    rootSigDesc.NumStaticSamplers = 0;
    rootSigDesc.pStaticSamplers = nullptr;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                             &serializedRootSig, &errorBlob);
    if (FAILED(hr))
    {
        MessageBoxA(nullptr, "Failed to serialize orb root signature", "Error", MB_OK);
        return;
    }

    hr = device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
                                     serializedRootSig->GetBufferSize(),
                                     IID_PPV_ARGS(&mOrbRootSignature));
    if (FAILED(hr))
    {
        MessageBoxA(nullptr, "Failed to create orb root signature", "Error", MB_OK);
        return;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
    ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

    psoDesc.VS = {
        reinterpret_cast<BYTE*>(mvsOrbByteCode->GetBufferPointer()),
        mvsOrbByteCode->GetBufferSize()
    };
    psoDesc.PS = {
        reinterpret_cast<BYTE*>(mpsOrbByteCode->GetBufferPointer()),
        mpsOrbByteCode->GetBufferSize()
    };

    psoDesc.InputLayout = { nullptr, 0 };   // квадрат строится в вершинном шейдере
    psoDesc.pRootSignature = mOrbRootSignature.Get();

    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    // Аддитивное смешивание: яркость лампочки прибавляется к уже освещённому кадру
    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    auto& rtBlend = psoDesc.BlendState.RenderTarget[0];
    rtBlend.BlendEnable = TRUE;
    rtBlend.SrcBlend = D3D12_BLEND_ONE;
    rtBlend.DestBlend = D3D12_BLEND_ONE;
    rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtBlend.DestBlendAlpha = D3D12_BLEND_ONE;
    rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // Глубина не нужна: лампочка гаснет раньше, чем коснётся геометрии
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mBackBufferFormat;
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mOrbPSO));
    if (FAILED(hr))
    {
        MessageBoxA(nullptr, "Failed to create orb PSO", "Error", MB_OK);
    }
}

// =========== Лампочки: геометрия для проверки касания ===========
// Sponza статичная, поэтому копию вершин и коробок треугольников достаточно
// подготовить один раз при загрузке модели.
void DirectXApp::BuildCollisionData(const std::vector<Vertex>& vertices,
                                    const std::vector<uint32_t>& indices)
{
    mCollisionPositions.clear();
    mCollisionIndices.clear();
    mTriangleBounds.clear();

    mCollisionPositions.reserve(vertices.size());
    for (const auto& v : vertices)
        mCollisionPositions.push_back(v.position);

    mCollisionIndices = indices;

    const size_t triCount = indices.size() / 3;
    mTriangleBounds.resize(triCount);

    mSceneBoundsMin = XMFLOAT3(FLT_MAX, FLT_MAX, FLT_MAX);
    mSceneBoundsMax = XMFLOAT3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    for (size_t tri = 0; tri < triCount; ++tri)
    {
        TriangleBounds& b = mTriangleBounds[tri];

        b.MinX = b.MinY = b.MinZ = FLT_MAX;
        b.MaxX = b.MaxY = b.MaxZ = -FLT_MAX;

        for (int k = 0; k < 3; ++k)
        {
            const XMFLOAT3& p = mCollisionPositions[indices[tri * 3 + k]];

            if (p.x < b.MinX) b.MinX = p.x;
            if (p.y < b.MinY) b.MinY = p.y;
            if (p.z < b.MinZ) b.MinZ = p.z;
            if (p.x > b.MaxX) b.MaxX = p.x;
            if (p.y > b.MaxY) b.MaxY = p.y;
            if (p.z > b.MaxZ) b.MaxZ = p.z;

            if (p.x < mSceneBoundsMin.x) mSceneBoundsMin.x = p.x;
            if (p.y < mSceneBoundsMin.y) mSceneBoundsMin.y = p.y;
            if (p.z < mSceneBoundsMin.z) mSceneBoundsMin.z = p.z;
            if (p.x > mSceneBoundsMax.x) mSceneBoundsMax.x = p.x;
            if (p.y > mSceneBoundsMax.y) mSceneBoundsMax.y = p.y;
            if (p.z > mSceneBoundsMax.z) mSceneBoundsMax.z = p.z;
        }
    }
}

// =========== Лампочки: где луч встретит модель ===========
// Луч идёт из точки рождения лампочки строго по направлению полёта.
// Возвращает расстояние до первой поверхности Sponza или -1, если луч её не задел.
// Сначала треугольники отсеиваются по своим коробкам, затем считается точное
// пересечение луча с треугольником (Мёллер-Трумбор).
float DirectXApp::RaycastSceneDistance(const XMFLOAT3& origin, const XMFLOAT3& dir)
{
    // Камера не двигалась — луч тот же самый, результат можно взять из кэша.
    if (mCachedRayValid &&
        fabsf(origin.x - mCachedRayOrigin.x) < 1e-6f &&
        fabsf(origin.y - mCachedRayOrigin.y) < 1e-6f &&
        fabsf(origin.z - mCachedRayOrigin.z) < 1e-6f &&
        fabsf(dir.x - mCachedRayDir.x) < 1e-6f &&
        fabsf(dir.y - mCachedRayDir.y) < 1e-6f &&
        fabsf(dir.z - mCachedRayDir.z) < 1e-6f)
    {
        return mCachedRayHit;
    }

    // Замер стоимости поиска: ~2.9 мс на первый луч (262 267 треугольников Sponza
    // в отладочной сборке). Повторный луч из той же точки берётся из кэша ниже.
    const size_t triCount = mTriangleBounds.size();

    if (triCount == 0)
        return -1.0f;

    const float org[3] = { origin.x, origin.y, origin.z };
    const float dr[3] = { dir.x, dir.y, dir.z };
    const float sceneMin[3] = { mSceneBoundsMin.x, mSceneBoundsMin.y, mSceneBoundsMin.z };
    const float sceneMax[3] = { mSceneBoundsMax.x, mSceneBoundsMax.y, mSceneBoundsMax.z };

    // 1) Отсечение по габаритной коробке всей модели.
    float tEnter = 0.0f;
    float tExit = FLT_MAX;

    for (int a = 0; a < 3; ++a)
    {
        if (fabsf(dr[a]) < 1e-8f)
        {
            if (org[a] < sceneMin[a] || org[a] > sceneMax[a])
                return -1.0f;
            continue;
        }

        float tNear = (sceneMin[a] - org[a]) / dr[a];
        float tFar = (sceneMax[a] - org[a]) / dr[a];
        if (tNear > tFar) { const float tmp = tNear; tNear = tFar; tFar = tmp; }

        if (tNear > tEnter) tEnter = tNear;
        if (tFar < tExit) tExit = tFar;

        if (tEnter > tExit)
            return -1.0f;   // луч прошёл мимо модели
    }

    // 2) Перебор треугольников: быстрый отсев по коробке, затем точная проверка.
    const XMFLOAT3* positions = mCollisionPositions.data();
    const uint32_t* indices = mCollisionIndices.data();

    float best = FLT_MAX;

    for (size_t tri = 0; tri < triCount; ++tri)
    {
        const TriangleBounds& b = mTriangleBounds[tri];
        const float triMin[3] = { b.MinX, b.MinY, b.MinZ };
        const float triMax[3] = { b.MaxX, b.MaxY, b.MaxZ };

        float boxEnter = tEnter;
        float boxExit = tExit;
        bool skip = false;

        for (int a = 0; a < 3 && !skip; ++a)
        {
            if (fabsf(dr[a]) < 1e-8f)
            {
                if (org[a] < triMin[a] || org[a] > triMax[a])
                    skip = true;
                continue;
            }

            float tNear = (triMin[a] - org[a]) / dr[a];
            float tFar = (triMax[a] - org[a]) / dr[a];
            if (tNear > tFar) { const float tmp = tNear; tNear = tFar; tFar = tmp; }

            if (tNear > boxEnter) boxEnter = tNear;
            if (tFar < boxExit) boxExit = tFar;

            if (boxEnter > boxExit)
                skip = true;
        }

        if (skip)
            continue;

        // Точное пересечение луча с треугольником
        const XMFLOAT3& p0 = positions[indices[tri * 3 + 0]];
        const XMFLOAT3& p1 = positions[indices[tri * 3 + 1]];
        const XMFLOAT3& p2 = positions[indices[tri * 3 + 2]];

        const float e1x = p1.x - p0.x, e1y = p1.y - p0.y, e1z = p1.z - p0.z;
        const float e2x = p2.x - p0.x, e2y = p2.y - p0.y, e2z = p2.z - p0.z;

        const float px = dr[1] * e2z - dr[2] * e2y;
        const float py = dr[2] * e2x - dr[0] * e2z;
        const float pz = dr[0] * e2y - dr[1] * e2x;

        const float det = e1x * px + e1y * py + e1z * pz;

        if (fabsf(det) < 1e-12f)
            continue;   // луч параллелен плоскости треугольника

        const float invDet = 1.0f / det;

        const float sx = org[0] - p0.x, sy = org[1] - p0.y, sz = org[2] - p0.z;

        const float u = (sx * px + sy * py + sz * pz) * invDet;
        if (u < 0.0f || u > 1.0f)
            continue;

        const float qx = sy * e1z - sz * e1y;
        const float qy = sz * e1x - sx * e1z;
        const float qz = sx * e1y - sy * e1x;

        const float v = (dr[0] * qx + dr[1] * qy + dr[2] * qz) * invDet;
        if (v < 0.0f || u + v > 1.0f)
            continue;

        const float dist = (e2x * qx + e2y * qy + e2z * qz) * invDet;

        if (dist > 1e-4f && dist < best)
            best = dist;
    }

    const float hit = (best < FLT_MAX) ? best : -1.0f;

    mCachedRayOrigin = origin;
    mCachedRayDir = dir;
    mCachedRayHit = hit;
    mCachedRayValid = true;

    return hit;
}

// =========== Лампочки: полёт, исчезновение, свет ===========
void DirectXApp::UpdateFlyingBulbs(float dt, FXMVECTOR pos, FXMVECTOR forwardVec, FXMVECTOR upVec)
{
    // 1) Движение строго по прямой: направление задано при рождении и не меняется.
    for (auto& bulb : mFlyingBulbs)
    {
        const float step = bulb.Speed * dt;

        bulb.Position.x += bulb.Direction.x * step;
        bulb.Position.y += bulb.Direction.y * step;
        bulb.Position.z += bulb.Direction.z * step;

        bulb.Distance += step;
    }

    // 2) Коснулась модели — исчезает мгновенно.
    //    Идём с конца, чтобы erase не сдвигал ещё не проверенные элементы.
    for (int i = (int)mFlyingBulbs.size() - 1; i >= 0; --i)
    {
        if (mFlyingBulbs[i].Distance >= mFlyingBulbs[i].MaxDistance)
            mFlyingBulbs.erase(mFlyingBulbs.begin() + i);
    }

    // 3) Рождение новой лампочки.
    //    Точка рождения берётся из камеры текущего кадра, поэтому при движении
    //    камеры место вылета переезжает вместе с ней.
    mBulbSpawnTimer += dt;

    if (mSpawnBulbs && mBulbSpawnTimer >= BulbSettings::Interval)
    {
        mBulbSpawnTimer = 0.0f;

        FlyingBulb bulb;

        XMStoreFloat3(&bulb.Position, pos - upVec * BulbSettings::BelowCameraOffset);
        XMStoreFloat3(&bulb.Direction, forwardVec);
        bulb.Speed = BulbSettings::Speed;

        // Где луч упрётся в Sponza, выясняем один раз при рождении: полёт прямой,
        // модель статичная — пересчитывать это каждый кадр незачем.
        const float hit = RaycastSceneDistance(bulb.Position, bulb.Direction);

        if (hit > 0.0f)
        {
            // минус радиус: лампочка исчезает в момент касания поверхности
            const float untilTouch = hit - BulbSettings::Radius;
            bulb.MaxDistance = (untilTouch > 0.0f) ? untilTouch : 0.0f;
        }
        else
        {
            bulb.MaxDistance = BulbSettings::FallbackDistance;
        }

        mFlyingBulbs.push_back(bulb);
    }

    // 4) Список источников для светового прохода: постоянные источники сцены плюс
    //    по одному точечному свету на каждую живую лампочку.
    mRenderLights = mLights;

    for (const auto& bulb : mFlyingBulbs)
    {
        mRenderLights.push_back(Light::CreatePointLight(
            bulb.Position,
            XMFLOAT3(BulbSettings::Color[0], BulbSettings::Color[1], BulbSettings::Color[2]),
            BulbSettings::LightIntensity,
            BulbSettings::LightRange));
    }
}

// =========== Shutdown ===========
void DirectXApp::Shutdown() {
    FlushCommandQueue();

    mPSO.Reset();
    mWireframePSO.Reset();
    mOrbPSO.Reset();
    mOrbRootSignature.Reset();
    mvsOrbByteCode.Reset();
    mpsOrbByteCode.Reset();
    mRootSignature.Reset();

    if (mRenderingSystem)
    {
        mRenderingSystem->Shutdown();
        mRenderingSystem.reset();
    }

    mObjectCB.reset();

    for (int i = 0; i < SwapChainBufferCount; i++) {
        mSwapChainBuffer[i].Reset();
    }
    mDepthStencilBuffer.Reset();
    mRtvHeap.Reset();
    mDsvHeap.Reset();
    mCbvHeap.Reset();
    mSwapChain.Reset();

    mVertexBufferGPU.Reset();
    mVertexBufferUploader.Reset();
    mIndexBufferGPU.Reset();
    mIndexBufferUploader.Reset();

    if (mCommandList) {
        mCommandList.Reset();
    }

    mFence.Reset();
    mDirectCmdListAlloc.Reset();
    mCommandQueue.Reset();
    device.Reset();
    adapter.Reset();
    dxgiFactory.Reset();
}

// =========== CreateDXGIFactory ===========
bool DirectXApp::CreateDXGIFactory() {
    UINT factoryFlags = 0;
    HRESULT hr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(hr)) {
        MessageBox(NULL, L"CreateDXGIFactory2 failed", L"Error", MB_OK);
        return false;
    }
    return true;
}

// =========== GetHardwareAdapter ===========
bool DirectXApp::GetHardwareAdapter() {
    ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(dxgiFactory.As(&factory6))) {
        for (UINT adapterIndex = 0; ; ++adapterIndex) {
            ComPtr<IDXGIAdapter1> currentAdapter;
            HRESULT hr = factory6->EnumAdapterByGpuPreference(
                adapterIndex,
                DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&currentAdapter));

            if (FAILED(hr)) break;

            DXGI_ADAPTER_DESC1 desc;
            currentAdapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

            if (SUCCEEDED(D3D12CreateDevice(currentAdapter.Get(),
                D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr))) {
                adapter = currentAdapter;
                return true;
            }
        }
    }
    return false;
}

// =========== CreateD3DDevice ===========
bool DirectXApp::CreateD3DDevice() {
    if (!GetHardwareAdapter()) {
        HRESULT hr = dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
        if (FAILED(hr)) {
            MessageBox(NULL, L"No hardware adapter found and WARP failed", L"Error", MB_OK);
            return false;
        }
    }

    HRESULT hr = D3D12CreateDevice(
        adapter.Get(),
        D3D_FEATURE_LEVEL_12_0,
        IID_PPV_ARGS(&device)
    );

    if (FAILED(hr)) {
        MessageBox(NULL, L"D3D12CreateDevice failed", L"Error", MB_OK);
        return false;
    }

    return true;
}

// =========== CreateCommandObjects ===========
bool DirectXApp::CreateCommandObjects() {
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    HRESULT hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue));
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create command queue", L"Error", MB_OK);
        return false;
    }

    hr = device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&mDirectCmdListAlloc)
    );
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create command allocator", L"Error", MB_OK);
        return false;
    }

    hr = device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        mDirectCmdListAlloc.Get(),
        nullptr,
        IID_PPV_ARGS(&mCommandList)
    );
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create command list", L"Error", MB_OK);
        return false;
    }

    mCommandList->Close();
    return true;
}

// =========== CreateFence ===========
bool DirectXApp::CreateFence() {
    HRESULT hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence));
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create fence", L"Error", MB_OK);
        return false;
    }
    mFenceValue = 0;
    return true;
}

// =========== FlushCommandQueue ===========
void DirectXApp::FlushCommandQueue() {
    mFenceValue++;
    mCommandQueue->Signal(mFence.Get(), mFenceValue);

    if (mFence->GetCompletedValue() < mFenceValue) {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        mFence->SetEventOnCompletion(mFenceValue, eventHandle);
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
}

// =========== CreateSwapChain ===========
bool DirectXApp::CreateSwapChain() {
    RECT clientRect;
    GetClientRect(window.GetHandle(), &clientRect);
    mClientWidth = clientRect.right - clientRect.left;
    mClientHeight = clientRect.bottom - clientRect.top;

    mSwapChain.Reset();

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = mClientWidth;
    sd.BufferDesc.Height = mClientHeight;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferDesc.Format = mBackBufferFormat;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = SwapChainBufferCount;
    sd.OutputWindow = window.GetHandle();
    sd.Windowed = true;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    HRESULT hr = dxgiFactory->CreateSwapChain(
        mCommandQueue.Get(),
        &sd,
        &mSwapChain
    );

    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create swap chain", L"Error", MB_OK);
        return false;
    }

    return true;
}

// =========== QueryDescriptorSizes ===========
void DirectXApp::QueryDescriptorSizes() {
    mRtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mDsvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    mCbvSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

// =========== CreateDescriptorHeaps ===========
bool DirectXApp::CreateDescriptorHeaps() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
    rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;

    HRESULT hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mRtvHeap));
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create RTV descriptor heap", L"Error", MB_OK);
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask = 0;

    hr = device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mDsvHeap));
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create DSV descriptor heap", L"Error", MB_OK);
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    cbvHeapDesc.NumDescriptors = 1 + 200;
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    cbvHeapDesc.NodeMask = 0;

    hr = device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCbvHeap));
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create CBV descriptor heap", L"Error", MB_OK);
        return false;
    }

    return true;
}

// =========== CreateRenderTargetViews ===========
bool DirectXApp::CreateRenderTargetViews() {
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < SwapChainBufferCount; i++) {
        ComPtr<ID3D12Resource> backBuffer;
        HRESULT hr = mSwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr)) {
            MessageBox(NULL, L"Failed to get swap chain buffer", L"Error", MB_OK);
            return false;
        }

        mSwapChainBuffer[i] = backBuffer;
        device->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
        rtvHeapHandle.ptr += mRtvDescriptorSize;
    }

    return true;
}

// =========== CreateDepthStencilBuffer ===========
bool DirectXApp::CreateDepthStencilBuffer() {
    D3D12_RESOURCE_DESC depthStencilDesc = {};
    depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthStencilDesc.Width = mClientWidth;
    depthStencilDesc.Height = mClientHeight;
    depthStencilDesc.DepthOrArraySize = 1;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.Format = mDepthStencilFormat;
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE optClear = {};
    optClear.Format = mDepthStencilFormat;
    optClear.DepthStencil.Depth = 1.0f;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &depthStencilDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,   // создаём сразу в состоянии записи глубины
        &optClear,
        IID_PPV_ARGS(&mDepthStencilBuffer)
    );

    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create depth stencil buffer", L"Error", MB_OK);
        return false;
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = mDepthStencilFormat;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

    device->CreateDepthStencilView(
        mDepthStencilBuffer.Get(),
        &dsvDesc,
        mDsvHeap->GetCPUDescriptorHandleForHeapStart()
    );

    return true;
}

// =========== CreateViewportAndScissor ===========
void DirectXApp::CreateViewportAndScissor() {
    mScreenViewport.TopLeftX = 0.0f;
    mScreenViewport.TopLeftY = 0.0f;
    mScreenViewport.Width = static_cast<float>(mClientWidth);
    mScreenViewport.Height = static_cast<float>(mClientHeight);
    mScreenViewport.MinDepth = 0.0f;
    mScreenViewport.MaxDepth = 1.0f;

    mScissorRect = { 0, 0, mClientWidth, mClientHeight };
}

// =========== Initialize ===========
bool DirectXApp::Initialize() {
#if defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();
        }
    }
#endif

    if (!CreateDXGIFactory()) return false;
    if (!CreateD3DDevice()) return false;
    if (!CreateCommandObjects()) return false;
    if (!CreateFence()) return false;
    if (!CreateSwapChain()) return false;
    QueryDescriptorSizes();
    if (!CreateDescriptorHeaps()) return false;
    if (!CreateRenderTargetViews()) return false;
    if (!CreateDepthStencilBuffer()) return false;
    CreateViewportAndScissor();

    BuildInputLayout();
    BuildObj("../assets/sponza.obj");

    std::vector<ParsedMaterial> parsed;
    LoadMTL("../assets/sponza.mtl", parsed);

    UINT srvIndex = 0;

    for (auto& p : parsed)
    {
        Material mat;
        mat.Name = p.Name;
        mat.SrvHeapIndex = srvIndex++;
        mat.DiffuseMap = p.DiffuseMap;

        if (!p.DiffuseMap.empty())
        {
            CreateTextureFromTGA("../assets/" + p.DiffuseMap, mat.DiffuseTexture);
        }
        else
        {
            CreateColorTexture(p.Kd, mat.DiffuseTexture);
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        // SRV диффузной текстуры (t0) — как в лабораторной 1
        D3D12_CPU_DESCRIPTOR_HANDLE hDescriptor = mCbvHeap->GetCPUDescriptorHandleForHeapStart();
        hDescriptor.ptr += (1 + mat.SrvHeapIndex) * mCbvSrvUavDescriptorSize;
        device->CreateShaderResourceView(mat.DiffuseTexture.Get(), &srvDesc, hDescriptor);

        mMaterials.push_back(mat);
    }

    BuildRootSignature();
    BuildShaders();
    BuildPSO();
    BuildWireframePSO();
    BuildOrbResources();
    BuildConstantBuffer();

    mCameraCB = std::make_unique<UploadBuffer<CameraConstants>>(
        device.Get(),
        1,
        true);

    // ===== СОЗДАНИЕ ИСТОЧНИКОВ СВЕТА =====
    mLights.clear();

    // Все источники — нейтрально-белые, чтобы камень выглядел так же, как в
    // лабораторной 1 (без жёлто-зелёного оттенка). Типы разные: ambient,
    // directional, point, spot — свет разнесён по сцене.
    //
    // Ambient
    mLights.push_back(Light::CreateAmbientLight(DirectX::XMFLOAT3(0.50f, 0.50f, 0.50f)));

    // Directional light (общий верхний свет)
    mLights.push_back(Light::CreateDirectionalLight(
        DirectX::XMFLOAT3(0.3f, -1.0f, 0.2f),
        DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f),
        0.9f));

    // Point light 1 (в центре атриума)
    mLights.push_back(Light::CreatePointLight(
        DirectX::XMFLOAT3(0.0f, 3.5f, 0.0f),
        DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f),
        0.9f, 18.0f));

    // Point light 2 (у боковой колоннады)
    mLights.push_back(Light::CreatePointLight(
        DirectX::XMFLOAT3(4.5f, 3.0f, 2.0f),
        DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f),
        0.7f, 14.0f));

    // Spot light (сверху вниз, по центральному проходу)
    mLights.push_back(Light::CreateSpotLight(
        DirectX::XMFLOAT3(0.0f, 6.0f, 0.0f),
        DirectX::XMFLOAT3(0.0f, -1.0f, 0.0f),
        DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f),
        1.4f, 25.0f, XM_PI / 3.0f));

    mLightingCB = std::make_unique<UploadBuffer<LightConstants>>(
        device.Get(),
        (UINT)mLights.size(),
        true);

    mRenderingSystem = std::make_unique<RenderingSystem>(
        device.Get(),
        mCommandQueue.Get(),
        mCommandList.Get(),
        mDirectCmdListAlloc.Get(),
        mFence.Get(),
        SwapChainBufferCount,
        mBackBufferFormat);

    if (!mRenderingSystem->Initialize(mClientWidth, mClientHeight))
    {
        MessageBox(NULL, L"Failed to initialize rendering system", L"Error", MB_OK);
        return false;
    }

    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * XM_PI,
        (float)mClientWidth / (float)mClientHeight, 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);

    mTimer.Reset();
    return true;
}

bool DirectXApp::InitializeApp() {
    return Initialize();
}

ID3D12Resource* DirectXApp::CurrentBackBuffer() const {
    return mSwapChainBuffer[mCurrBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXApp::CurrentBackBufferView() const {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += mCurrBackBuffer * mRtvDescriptorSize;
    return handle;
}

void DirectXApp::OnResize() {
}

void DirectXApp::OnKeyDown(WPARAM wParam)
{
    HWND activeWindow = GetActiveWindow();
    if (activeWindow != window.GetHwnd()) {
        return;
    }

    if (wParam == VK_F2) {
        mWireframeMode = !mWireframeMode;
    }

    if (wParam == 'T') {
        mAnimateTextures = !mAnimateTextures;
    }

    if (wParam == 'R') {
        mUVScaleU = 1.0f;
        mUVScaleV = 1.0f;
        mUVOffsetU = 0.0f;
        mUVOffsetV = 0.0f;
    }

    // L — включать/выключать вылет лампочек из-под камеры
    if (wParam == 'L') {
        mSpawnBulbs = !mSpawnBulbs;
        // первая лампочка вылетает сразу после включения
        if (mSpawnBulbs) mBulbSpawnTimer = BulbSettings::Interval;
    }

    // Отладочный вывод слоёв G-буфера: 0 - освещение, 1 - albedo,
    // 2 - нормали, 3 - глубина, 4 - блик
    if (wParam >= '0' && wParam <= '4') {
        mDebugMode = (int)(wParam - '0');
    }
}

int DirectXApp::Run() {
    MSG msg = { 0 };
    mTimer.Reset();

    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            mTimer.Tick();
            if (!mAppPaused) {
                CalculateFrameStats();
                Update(mTimer);
                Draw(mTimer);
            }
            else {
                Sleep(100);
            }
        }
    }
    return (int)msg.wParam;
}


void DirectXApp::CalculateFrameStats() {
    mFrameCount++;
    if ((mTimer.TotalTime() - mTimeElapsed) >= 1.0f) {
        float fps = (float)mFrameCount;
        float mspf = 1000.0f / fps;

        std::wstring windowText = mMainWndCaption;
        windowText += L" FPS: " + std::to_wstring(fps);
        windowText += L" MSPF: " + std::to_wstring(mspf);
        windowText += L" Bulbs: " + std::to_wstring(mFlyingBulbs.size());
        windowText += mSpawnBulbs ? L" (L ON)" : L" (L off)";


        SetWindowText(window.GetHandle(), windowText.c_str());

        mFrameCount = 0;
        mTimeElapsed += 1.0f;
    }
}

void DirectXApp::Update(const Timer& gt)
{
    float dt = gt.DeltaTime();
    float speed = 50.0f;

    // ===== Forward Vector =====
    XMFLOAT3 forward =
    {
        cosf(mPitch) * cosf(mYaw),
        sinf(mPitch),
        cosf(mPitch) * sinf(mYaw)
    };

    XMVECTOR forwardVec = XMLoadFloat3(&forward);
    forwardVec = XMVector3Normalize(forwardVec);

    XMVECTOR rightVec = XMVector3Normalize(
        XMVector3Cross(XMVectorSet(0, 1, 0, 0), forwardVec));
    XMVECTOR upVec = XMVector3Normalize(
        XMVector3Cross(forwardVec, rightVec));

    // ===== Movement =====
    XMVECTOR pos = XMLoadFloat3(&mEyePos);

    if (GetAsyncKeyState('W') & 0x8000)
        pos += forwardVec * speed * dt;
    if (GetAsyncKeyState('S') & 0x8000)
        pos -= forwardVec * speed * dt;
    if (GetAsyncKeyState('A') & 0x8000)
        pos -= rightVec * speed * dt;
    if (GetAsyncKeyState('D') & 0x8000)
        pos += rightVec * speed * dt;
    if (GetAsyncKeyState(VK_UP) & 0x8000)
        pos += XMVectorSet(0, 1, 0, 0) * speed * dt;
    if (GetAsyncKeyState(VK_DOWN) & 0x8000)
        pos -= XMVectorSet(0, 1, 0, 0) * speed * dt;

    XMStoreFloat3(&mEyePos, pos);

    // Базис камеры: лампочкам нужны направление полёта и ориентация билборда.
    XMStoreFloat3(&mCamRight, rightVec);
    XMStoreFloat3(&mCamUp, upVec);
    XMStoreFloat3(&mCamForward, forwardVec);

    // ===== ЛЕТЯЩИЕ ЛАМПОЧКИ (клавиша L) =====
    UpdateFlyingBulbs(dt, pos, forwardVec, upVec);

    // ===== View Matrix =====
    XMMATRIX view = XMMatrixLookToLH(pos, forwardVec, upVec);
    XMStoreFloat4x4(&mView, view);

    // ===== Projection =====
    XMMATRIX proj = XMMatrixPerspectiveFovLH(
        XM_PIDIV4,
        (float)mClientWidth / (float)mClientHeight,
        0.1f, 1000.0f);
    XMStoreFloat4x4(&mProj, proj);

    XMMATRIX viewProj = view * proj;
    XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

    CameraConstants camConstants;
    XMStoreFloat4x4(&camConstants.mInvViewProj, XMMatrixTranspose(invViewProj));
    camConstants.mCameraPos = mEyePos;
    camConstants.mDebugMode = mDebugMode;
    camConstants.mScreenSize = { (float)mClientWidth, (float)mClientHeight };
    mCameraCB->CopyData(0, camConstants);

    // ===== TEXTURE ANIMATION =====
    if (mAnimateTextures)
    {
        mUVOffsetU += dt * 0.1f;
        mUVOffsetV += dt * 0.05f;
        if (mUVOffsetU > 1.0f) mUVOffsetU -= 1.0f;
        if (mUVOffsetV > 1.0f) mUVOffsetV -= 1.0f;
    }

    // ===== WVP и параметры =====
    XMMATRIX world = XMMatrixIdentity();
    XMMATRIX worldViewProj = world * view * proj;

    ObjectConstants objConstants;
    XMStoreFloat4x4(&objConstants.mWorld, XMMatrixTranspose(world));
    XMStoreFloat4x4(&objConstants.mWorldViewProj, XMMatrixTranspose(worldViewProj));
    objConstants.mUVTransform = XMFLOAT4(mUVScaleU, mUVScaleV, mUVOffsetU, mUVOffsetV);
    mObjectCB->CopyData(0, objConstants);
}

void DirectXApp::Draw(const Timer& gt)
{
    mRenderingSystem->GeometryPass(
        mPSO.Get(),
        mRootSignature.Get(),
        mCbvHeap.Get(),
        mCbvSrvUavDescriptorSize,
        mSubmeshes,
        mMaterials,
        mVertexBufferGPU.Get(),
        mIndexBufferGPU.Get(),
        mVertexBufferView,
        mIndexBufferView,
        mDepthStencilBuffer.Get(),
        DepthStencilView(),
        mScreenViewport,
        mScissorRect,
        (UINT)mMaterials.size());

    // Матрица вида-проекции для билбордов лампочек (в шейдер уходит транспонированной,
    // как и все матрицы в этой лабе).
    XMMATRIX viewProj = XMLoadFloat4x4(&mView) * XMLoadFloat4x4(&mProj);
    XMFLOAT4X4 viewProjTransposed;
    XMStoreFloat4x4(&viewProjTransposed, XMMatrixTranspose(viewProj));

    mRenderingSystem->LightingPass(
        CurrentBackBuffer(),
        CurrentBackBufferView(),
        mRenderLights,          // постоянные источники сцены + летящие лампочки
        mEyePos,
        mScreenViewport,
        mScissorRect,
        mCurrBackBuffer,
        mSwapChain.Get(),
        mRenderingSystem->GetLightingPSO(),
        mRenderingSystem->GetLightingRootSignature(),
        mRenderingSystem->GetLightingCB(),
        mCameraCB.get(),
        mRenderingSystem->GetGBuffer(),
        mDebugMode,
        mOrbPSO.Get(),
        mOrbRootSignature.Get(),
        &mFlyingBulbs,
        &viewProjTransposed,
        mCamRight,
        mCamUp);

    FlushCommandQueue();
}

void DirectXApp::CreateTextureFromTGA(
    const std::string& path,
    Microsoft::WRL::ComPtr<ID3D12Resource>& texture)
{
    TgaImage image;
    if (!LoadTGA(path, image))
    {
        throw std::runtime_error("Failed to load TGA: " + path);
    }

    UINT pixelSize = image.data.size() / (image.width * image.height);

    if (pixelSize == 3)
    {
        std::vector<uint8_t> converted;
        converted.resize(image.width * image.height * 4);

        for (UINT i = 0; i < image.width * image.height; i++)
        {
            converted[i * 4 + 0] = image.data[i * 3 + 0];
            converted[i * 4 + 1] = image.data[i * 3 + 1];
            converted[i * 4 + 2] = image.data[i * 3 + 2];
            converted[i * 4 + 3] = 255;
        }

        image.data = std::move(converted);
    }

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = image.width;
    texDesc.Height = image.height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture)));

    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(
        &texDesc, 0, 1, 0,
        nullptr, nullptr, nullptr,
        &uploadSize);

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = uploadSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.SampleDesc.Count = 1;

    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;

    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)));

    void* mapped = nullptr;
    uploadBuffer->Map(0, nullptr, &mapped);

    BYTE* dest = reinterpret_cast<BYTE*>(mapped);
    BYTE* srcData = image.data.data();

    UINT rowPitch = (image.width * 4 + 255) & ~255;

    for (UINT y = 0; y < image.height; y++)
    {
        memcpy(dest + y * rowPitch, srcData + y * image.width * 4, image.width * 4);
    }

    uploadBuffer->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = texture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = uploadBuffer.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;

    device->GetCopyableFootprints(
        &texDesc, 0, 1, 0,
        &src.PlacedFootprint,
        nullptr, nullptr, nullptr);

    mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr);
    mCommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    mCommandList->ResourceBarrier(1, &barrier);
    mCommandList->Close();

    ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, cmdLists);

    FlushCommandQueue();
}


void DirectXApp::CreateColorTexture(
    const DirectX::XMFLOAT3& color,
    Microsoft::WRL::ComPtr<ID3D12Resource>& texture)
{
    UINT r = (UINT)(color.x * 255.0f);
    UINT g = (UINT)(color.y * 255.0f);
    UINT b = (UINT)(color.z * 255.0f);

    UINT pixel = (255 << 24) | (b << 16) | (g << 8) | r;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = 1;
    texDesc.Height = 1;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture)));

    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(
        &texDesc, 0, 1, 0,
        nullptr, nullptr, nullptr,
        &uploadSize);

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = uploadSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.SampleDesc.Count = 1;

    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;

    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)));

    void* mapped = nullptr;
    uploadBuffer->Map(0, nullptr, &mapped);
    memcpy(mapped, &pixel, sizeof(UINT));
    uploadBuffer->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = texture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = uploadBuffer.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;

    device->GetCopyableFootprints(
        &texDesc, 0, 1, 0,
        &src.PlacedFootprint,
        nullptr, nullptr, nullptr);

    mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr);
    mCommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    mCommandList->ResourceBarrier(1, &barrier);
    mCommandList->Close();

    ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, cmdLists);

    FlushCommandQueue();
}
