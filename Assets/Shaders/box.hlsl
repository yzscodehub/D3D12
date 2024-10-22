struct ConstBufferObject
{
    float4x4 gWorldViewProj;
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
    ConstBufferObject cbo;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    vout.pos = mul(float4(vin.pos, 1.0f), cbo.gWorldViewProj);
    vout.color = vin.color;
    return vout;
}

float4 PS(VertexOut pin) : SV_TARGET
{
    return pin.color;
}