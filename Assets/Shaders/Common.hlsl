// Defaults for number of lights.
#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif


#include "LightingUtil.hlsl"

// 每帧都在变化的单个模型的常量数据
struct InstanceData
{
    float4x4 world;
    float4x4 texTransform;
    uint materialIndex;
    uint objPad0;
    uint objPad1;
    uint objPad2;
};

// 材质数据
struct MaterialData
{
    float4 diffuseAlbedo;
    float3 fresnelR0;
    float roughness;
    float4x4 matTransform;
    uint diffuseMapIndex;
    uint matPad0;
    uint matPad1;
    uint matPad2;
};

// 绘制过程中所用的杂项常量数据
struct ConstBufferPass
{
    float4x4 view;
    float4x4 invView;
    float4x4 proj;
    float4x4 invProj;
    float4x4 viewProj;
    float4x4 invViewProj;
    float3 eyePosW;
    float perObjectPad1;
    float2 renderTargetSize;
    float2 invRenderTargetSize;
    float nearZ;
    float farZ;
    float totalTime;
    float deltaTime;
    
    // 环境光
    float4 ambientLight;
    
    // Fog
    float4 fogColor;
    float fogStart;
    float fogRange;
    float2 perObjectPad2;
    
    // Lights
    Light lights[MaxLights];
};

SamplerState gSamPointWrap : register(s0);
SamplerState gSamPointClamp : register(s1);
SamplerState gSamLinearWrap : register(s2);
SamplerState gSamLinearClamp : register(s3);
SamplerState gSamAnisotropicWrap : register(s4);
SamplerState gSamAnisotropicClamp : register(s5);

cbuffer ConstBuffer : register(b0)
{
    ConstBufferPass cbPass;
};
TextureCube gCubeMap : register(t0);
Texture2D gDiffuseMap[12] : register(t1);

StructuredBuffer<InstanceData> gInstanceData : register(t0, space1);
StructuredBuffer<MaterialData> gMaterialData : register(t1, space1);

