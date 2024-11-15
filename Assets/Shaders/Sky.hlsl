
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
    float3 posL : POSITION;
};

VertexOut VS(VertexIn vin, uint instanceID : SV_InstanceID)
{
    VertexOut vout;
    
    InstanceData instanceData = gInstanceData[instanceID];
    
    // 用局部顶点的位置作为立方体图的查找向量
    vout.posL = vin.pos;
    
    // 把顶点变换到世界空间
    float4 posW = mul(float4(vin.pos, 1.0), instanceData.world);
    
    // 总是以摄像机作为天空球的中心
    posW.xyz += cbPass.eyePosW;
    
    // 设置z=w,从而使z/w=1(即令球面总是位于远平面)
    vout.posH = mul(posW, cbPass.viewProj).xyww;
    
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    return gCubeMap.Sample(gSamLinearWrap, pin.posL);
}