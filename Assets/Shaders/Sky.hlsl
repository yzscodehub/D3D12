
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
    
    // �þֲ������λ����Ϊ������ͼ�Ĳ�������
    vout.posL = vin.pos;
    
    // �Ѷ���任������ռ�
    float4 posW = mul(float4(vin.pos, 1.0), instanceData.world);
    
    // �������������Ϊ����������
    posW.xyz += cbPass.eyePosW;
    
    // ����z=w,�Ӷ�ʹz/w=1(������������λ��Զƽ��)
    vout.posH = mul(posW, cbPass.viewProj).xyww;
    
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    return gCubeMap.Sample(gSamLinearWrap, pin.posL);
}