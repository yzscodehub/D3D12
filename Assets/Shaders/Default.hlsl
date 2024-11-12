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

// 每帧都在变化的单个模型的常量数据
struct ConstBufferObject
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

Texture2D gDiffuseMap[7] : register(t0);

StructuredBuffer<MaterialData> gMaterialData : register(t0, space1);

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

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    
    MaterialData matData = gMaterialData[cbPerObject.materialIndex];
    
    // 将顶点变换到世界空间
    float4 worldPos = mul(float4(vin.pos, 1.0f), cbPerObject.world);
    vout.posW = worldPos.xyx;
    
    // TODO 假设这里进行的是等比缩放，否则这里需要使用世界矩阵的逆转置矩阵
    vout.normal = mul(vin.normal, (float3x3) cbPerObject.world);
    
    // 将顶点变换到齐次裁剪空间
    vout.posH = mul(worldPos, cbPass.viewProj);
    
    // 为三角形插值而输出顶点属性
    float4 texC = mul(float4(vin.texCoord, 0.0f, 1.0f), cbPerObject.texTransform);
    vout.texCoord = mul(texC, matData.matTransform).xy;
    return vout;
}


float4 PS(VertexOut pin) : SV_TARGET
{
    MaterialData matData = gMaterialData[cbPerObject.materialIndex];
    float4 diffuseAlbedo = gDiffuseMap[matData.diffuseMapIndex].Sample(gSamAnisotropicWrap, pin.texCoord) * matData.diffuseAlbedo;
    
#ifdef ALPHA_TEST
    // 若alpha < 0.1 则抛弃该像素。我们要在着色器中尽早执行此项测试，以尽快检测出满足条件的像素并退出着色器，从而跳出后续的相关处理
    clip(diffuseAlbedo.a - 0.1f);
#endif
    
    // 对法线插值可能导致其非规范化，因此需要再次对它进行规范化处理
    pin.normal = normalize(pin.normal);
    
    // 光线经表面上一点反射到观察点的向量
    float3 toEye = cbPass.eyePosW - pin.posW;
    float distToEye = length(toEye);
    toEye /= distToEye;
    
    // 间接关照
    float4 ambient = cbPass.ambientLight * diffuseAlbedo;
    
    const float shininess = 1.0f - matData.roughness;
    Material mat = { diffuseAlbedo, matData.fresnelR0, shininess };
    float3 shadowFactor = 1.0f;
    float4 lightingResult = ComputeLighting(cbPass.lights, mat, pin.posW, pin.normal, toEye, shadowFactor);
    
    float4 litColor = ambient + lightingResult;
    
#ifdef FOG
    float fogAmount = saturate((distToEye-cbPass.fogStart)/cbPass.fogRange);
    litColor = lerp(litColor, cbPass.fogColor, fogAmount);
#endif
    
    // 从漫反射材质中获取alpha值的常见手段
    litColor.a = diffuseAlbedo.a;
    
    return litColor;
}