#pragma once
#include <DirectXMath.h>

struct CameraConstants
{
    DirectX::XMFLOAT4X4 mInvViewProj;
    DirectX::XMFLOAT3 mCameraPos;
    int mDebugMode;                  // 0 - освещение, 1..4 - слой G-буфера
    DirectX::XMFLOAT2 mScreenSize;
    DirectX::XMFLOAT2 mPadding;

    CameraConstants()
    {
        DirectX::XMStoreFloat4x4(&mInvViewProj, DirectX::XMMatrixIdentity());
        mCameraPos = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
        mDebugMode = 0;
        mScreenSize = DirectX::XMFLOAT2(800.0f, 600.0f);
        mPadding = DirectX::XMFLOAT2(0.0f, 0.0f);
    }
};
