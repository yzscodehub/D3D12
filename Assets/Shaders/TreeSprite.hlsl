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
    float4x4 gWorld;
    float4x4 gTexTransform;
};

// 每种材质都各有区别的常量数据
struct ConstBufferMaterial
{
    float4 gDiffuseAlbebo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTransform;
};

// 绘制过程中所用的杂项常量数据
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
    float2 size : SIZE;
};

struct VertexOut
{
    float3 center : POSITION;
    float2 size : SIZE;
};

struct GeoOut
{
    float4 posH     : SV_Position;
    float3 posW     : POSITIONT;
    float3 normal   : NORMAL;
    float2 texCoord : TEXCOORD;
    uint   primID   : SV_PrimitiveID;
};

Texture2DArray gTreeMapArray : register(t0);

SamplerState gSamPointWrap : register(s0);
SamplerState gSamPointClamp : register(s1);
SamplerState gSamLinearWrap : register(s2);
SamplerState gSamLinearClamp : register(s3);
SamplerState gSamAnisotropicWrap : register(s4);
SamplerState gSamAnisotropicClamp : register(s5);

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
    
    // 直接将数据传入几何着色器
    vout.center = vin.pos;
    vout.size = vin.size;
    
    return vout;
}

// 由于我们要将每个顶点都扩展为一个四边形，因此每次调用几何着色器最多输出4个顶点
[maxvertexcount(4)]
void GS(point VertexOut gin[1], 
        uint primID : SV_PrimitiveID, 
        inout TriangleStream<GeoOut> triStream)
{
    // 计算Sprite的局部坐标系与世界空间的相对关系，以使公告牌与y轴对称且面向观察者
    float3 up = float3(0.0f, 1.0f, 0.0f);
    float3 look = cbPass.gEyePosW - gin[0].center;
    look.y = 0.0f;  // 与y轴对称，以此使公告牌立于xz平面
    look = normalize(look);
    float3 right = cross(up, look);
    
    // 计算世界空间中三角形带的顶点
    float halfWidth = 0.5f * gin[0].size.x;
    float halfHeight = 0.5f * gin[0].size.y;
    float4 vertices[4];
    vertices[0] = float4(gin[0].center + halfWidth * right - halfHeight * up, 1.0f);
    vertices[1] = float4(gin[0].center + halfWidth * right + halfHeight * up, 1.0f);
    vertices[2] = float4(gin[0].center - halfWidth * right - halfHeight * up, 1.0f);
    vertices[3] = float4(gin[0].center - halfWidth * right + halfHeight * up, 1.0f);
    
    float2 texCoords[4] =
    {
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(1.0f, 0.0f),
    };
    
    GeoOut gout;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        gout.posH = mul(vertices[i], cbPass.gViewProj);
        gout.posW = vertices[i].xyz;
        gout.normal = look;
        gout.texCoord = texCoords[i];
        gout.primID = primID;
        
        triStream.Append(gout);
    }
}


float4 PS(GeoOut pin) : SV_TARGET
{
    float3 uvw = float3(pin.texCoord, pin.primID % 3);
    float4 diffuseAlbedo = gTreeMapArray.Sample(gSamAnisotropicWrap, uvw) * cbMaterial.gDiffuseAlbebo;
    
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