#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 1
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

#include "LightingUtil.hlsl"


struct ConstBufferObject
{
    float4x4 gWorld;
};

struct ConstBufferMaterial
{
    float4 gDiffuseAlbebo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTransform;
};

struct ConstBufferPass
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;
    
    Light gLights[MaxLights];
};


struct VertexIn
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
};

struct VertexOut
{
    float4 posH : SV_POSITION;
    float3 posW : POSITION;
    float3 normal : NORMAL;
};


cbuffer ConstBuffer : register(b0)
{
    ConstBufferObject cbPerObject;
};

cbuffer ConstBuffer : register(b1)
{
    ConstBufferMaterial cbMaterial;
};

cbuffer ConstBuffer : register(b2)
{
    ConstBufferPass cbPass;
};


VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    
    // 将顶点变换到世界空间
    float4 worldPos = mul(float4(vin.pos, 1.0f), cbPerObject.gWorld);
    vout.posW = worldPos.xyx;
    
    // TODO 假设这里进行的是等比缩放，否则这里需要使用世界矩阵的逆转置矩阵
    vout.normal = mul(vin.normal, (float3x3) cbPerObject.gWorld);
    
    // 将顶点变换到齐次裁剪空间
    vout.posH = mul(worldPos, cbPass.gViewProj);
    return vout;
}


float4 PS(VertexOut pin) : SV_TARGET
{
    // 对法线插值可能导致其非规范化，因此需要再次对它进行规范化处理
    pin.normal = normalize(pin.normal);
    
    // 光线经表面上一点反射到观察点的向量
    float3 toEye = normalize(cbPass.gEyePosW - pin.posW);
    
    // 间接关照
    float4 ambient = cbPass.gAmbientLight * cbMaterial.gDiffuseAlbebo;
    
    const float shininess = 1.0f - cbMaterial.gRoughness;
    Material mat = { cbMaterial.gDiffuseAlbebo, cbMaterial.gFresnelR0, shininess };
    float3 shadowFactor = 1.0f;
    float4 lightingResult = ComputeLighting(cbPass.gLights, mat, pin.posW, pin.normal, toEye, shadowFactor);
    
    float4 litColor = ambient + lightingResult;
    
    // 从漫反射材质中获取alpha值的常见手段
    litColor.a = cbMaterial.gDiffuseAlbebo.a;
    
    return litColor;
}