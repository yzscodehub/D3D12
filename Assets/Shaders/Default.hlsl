#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3
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
    float4x4 gTexTransform;
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
    
    // 环境光
    float4 gAmbientLight;
    
    // Fog
    float4 gFogColor;
    float gFogStart;
    float gFogRange;
    float2 cbPerObjectPad2;
    
    // Lights
    Light gLights[MaxLights];
};


struct VertexIn
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
};

struct VertexOut
{
    float4 posH : SV_POSITION;
    float3 posW : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
};


Texture2D gDiffuseMap : register(t0);

SamplerState gSamPointWrap          : register(s0);
SamplerState gSamPointClamp         : register(s1);
SamplerState gSamLinearWrap         : register(s2);
SamplerState gSamLinearClamp        : register(s3);
SamplerState gSamAnisotropicWrap    : register(s4);
SamplerState gSamAnisotropicClamp   : register(s5);

cbuffer ConstBuffer : register(b0)
{
    ConstBufferObject cbPerObject;
};

cbuffer ConstBuffer : register(b1)
{
    ConstBufferPass cbPass;
};

cbuffer ConstBuffer : register(b2)
{
    ConstBufferMaterial cbMaterial;
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
    
    // 为三角形插值而输出顶点属性
    float4 texC = mul(float4(vin.texCoord, 0.0f, 1.0f), cbPerObject.gTexTransform);
    vout.texCoord = mul(texC, cbMaterial.gMatTransform).xy;
    return vout;
}


float4 PS(VertexOut pin) : SV_TARGET
{
    float4 diffuseAlbedo = gDiffuseMap.Sample(gSamAnisotropicWrap, pin.texCoord) * cbMaterial.gDiffuseAlbebo;
    
#ifdef ALPHA_TEST
    // 若alpha < 0.1 则抛弃该像素。我们要在着色器中尽早执行此项测试，以尽快检测出满足条件的像素并退出着色器，从而跳出后续的相关处理
    clip(diffuseAlbedo.a - 0.1f);
#endif
    
    // 对法线插值可能导致其非规范化，因此需要再次对它进行规范化处理
    pin.normal = normalize(pin.normal);
    
    // 光线经表面上一点反射到观察点的向量
    float3 toEye = cbPass.gEyePosW - pin.posW;
    float distToEye = length(toEye);
    toEye /= distToEye;
    
    // 间接关照
    float4 ambient = cbPass.gAmbientLight * diffuseAlbedo;
    
    const float shininess = 1.0f - cbMaterial.gRoughness;
    Material mat = { diffuseAlbedo, cbMaterial.gFresnelR0, shininess };
    float3 shadowFactor = 1.0f;
    float4 lightingResult = ComputeLighting(cbPass.gLights, mat, pin.posW, pin.normal, toEye, shadowFactor);
    
    float4 litColor = ambient + lightingResult;
    
#ifdef FOG
    float fogAmount = saturate((distToEye-cbPass.gFogStart)/cbPass.gFogRange);
    litColor = lerp(litColor, cbPass.gFogColor, fogAmount);
#endif
    
    // 从漫反射材质中获取alpha值的常见手段
    litColor.a = diffuseAlbedo.a;
    
    return litColor;
}