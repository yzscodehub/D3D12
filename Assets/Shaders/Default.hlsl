
#include "Common.hlsl"


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
    // 带有 nointerpolation 的变量不会根据片元的位置进行插值，而是在整个图元内保持一致
    nointerpolation uint materialIndex : MATERIALINDEX;
};


VertexOut VS(VertexIn vin, uint instanceID : SV_InstanceID)
{
    VertexOut vout;
    
    InstanceData instData = gInstanceData[instanceID];
    float4x4 world = instData.world;
    float4x4 texTransform = instData.texTransform;
    uint materialIndex = instData.materialIndex;
    
    MaterialData matData = gMaterialData[materialIndex];
    
    // 将顶点变换到世界空间
    float4 worldPos = mul(float4(vin.pos, 1.0f), world);
    vout.posW = worldPos.xyx;
    
    // TODO 假设这里进行的是等比缩放，否则这里需要使用世界矩阵的逆转置矩阵
    vout.normal = mul(vin.normal, (float3x3) world);
    
    // 将顶点变换到齐次裁剪空间
    vout.posH = mul(worldPos, cbPass.viewProj);
    
    // 为三角形插值而输出顶点属性
    float4 texC = mul(float4(vin.texCoord, 0.0f, 1.0f), texTransform);
    vout.texCoord = mul(texC, matData.matTransform).xy;
    
    vout.materialIndex = materialIndex;
    
    return vout;
}


float4 PS(VertexOut pin) : SV_TARGET
{
    MaterialData matData = gMaterialData[pin.materialIndex];
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
    
    // 加入镜面反射数据
    float3 r = reflect(-toEye, pin.normal);
    float4 reflectionColor = gCubeMap.Sample(gSamLinearWrap, r);
    float3 fresnelFactor = SchlickFresnel(matData.fresnelR0, pin.normal, r);
    litColor.rgb += shininess * fresnelFactor * reflectionColor.rgb;
    
    // 从漫反射材质中获取alpha值的常见手段
    litColor.a = diffuseAlbedo.a;
    
    return litColor;
}