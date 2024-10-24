struct ConstBufferObject
{
    float4x4 gWorld;
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
};


struct VertexIn
{
    float3 pos : POSITION;
    float4 color : COLOR;
};

struct VertexOut
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};


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
    float4 position = mul(float4(vin.pos, 1.0f), cbPerObject.gWorld);
    vout.pos = mul(position, cbPass.gViewProj);
    
    vout.color = vin.color;
    return vout;
}

float4 PS(VertexOut pin) : SV_TARGET
{
    return pin.color;
}