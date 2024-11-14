#pragma once

#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "FrameResource.h"

struct InstanceData
{
    DirectX::XMFLOAT4X4 world = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 texTransform = MathHelper::Identity4x4();
    UINT materialIndex;
    UINT objPad0;
    UINT objPad1;
    UINT objPad2;
};

struct PassConstants
{
    DirectX::XMFLOAT4X4 view = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 invView = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 proj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 invProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 viewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 invViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT3 eyePos = {0.0f, 0.0f, 0.0f};
    float cbPerObjectPad1 = 0.0f;
    DirectX::XMFLOAT2 renderTargetSize = {0.0f, 0.0f};
    DirectX::XMFLOAT2 invRenderTargetSize = {0.0f, 0.0f};
    float nearZ = 0.0f;
    float farZ = 0.0f;
    float totalTime = 0.0f;
    float deltaTime = 0.0f;

    DirectX::XMFLOAT4 ambientLight = {0.0f, 0.0f, 0.0f, 1.0f};

    // 允许应用程序在每一帧都能改变雾效参数，例如，我们可能在一天中的特定时间才使用雾效
    DirectX::XMFLOAT4 gFogColor = {0.7f, 0.7f, 0.7f, 1.0f};
    float gFogStart = 5.0f;
    float gFogRange = 150.0f;
    DirectX::XMFLOAT2 cbPerObjectPad2;

    // Indices [0, NUM_DIR_LIGHTS) are directional lights;
    // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
    // indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS) are spot lights for a maximum of MaxLights per object.
    Light lights[MaxLights];
};

struct Vertex
{
    Vertex() = default;
    Vertex(float x, float y, float z, float nx, float ny, float nz, float u, float v)
        : pos(x, y, z)
        , normal(nx, ny, nz)
        , texCoord(u, v)
    {}

    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 texCoord;
};

struct TreeSpriteVertex
{
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT2 size;
};

struct MaterialData
{
    DirectX::XMFLOAT4 diffuseAlbedo = {1.0f, 1.0f, 1.0f, 1.0f};
    DirectX::XMFLOAT3 fresnelR0 = {0.01f, 0.01f, 0.01f};
    float roughness = 64.0f;

    // Used in texture mapping.
    DirectX::XMFLOAT4X4 matTransform = MathHelper::Identity4x4();

    UINT diffuseMapIndex = 0;
    UINT materialPad0;
    UINT materialPad1;
    UINT materialPad2;
};

struct FrameResource
{
public:
    FrameResource(
        ID3D12Device *device,
        uint32_t passCount,
        uint32_t objectCount,
        uint32_t materialCount,
        uint32_t waveVertexCount);

    FrameResource(FrameResource &rhs) = delete;
    FrameResource &operator=(const FrameResource &rhs) = delete;
    ~FrameResource();

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> cmdListAlloc;

    std::unique_ptr<UploadBuffer<PassConstants>> passCB = nullptr;
    std::unique_ptr<UploadBuffer<MaterialData>> materialBuffer = nullptr;
    std::unique_ptr<UploadBuffer<InstanceData>> instanceBuffer = nullptr;

    std::unique_ptr<UploadBuffer<Vertex>> wavesVB = nullptr;

    uint64_t fence = 0;
};