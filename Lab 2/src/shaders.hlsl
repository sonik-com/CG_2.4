// Геометрический проход отложенного рендеринга.
// Материал — одна диффузная текстура, как в лабораторной 1
// (смешивание двух текстур, из-за которого текстуры «мигали», убрано).

Texture2D gDiffuseMap : register(t0);
SamplerState gSampler : register(s0);

cbuffer cbPerObject : register(b0)
{
    float4x4 mWorld;
    float4x4 mWorldViewProj;
    float4 mUVTransform;  // xy = scale, zw = offset
};

// Параметры блика текущего материала (root constants, b2)
cbuffer cbMaterial : register(b2)
{
    float3 gSpecularColor;
    float gSpecularPower;
};

struct VSInput
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 Tex : TEXCOORD;
};

struct VSOutput
{
    float4 PosH : SV_POSITION;
    float3 WorldPos : POSITION0;
    float3 Normal : NORMAL0;
    float2 TexC : TEXCOORD0;
};

// G-buffer: 4 render target'а
struct PSOutput
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
    float Depth : SV_Target2;
    float4 Specular : SV_Target3;
};

VSOutput VS(VSInput vin)
{
    VSOutput vout;

    float4 worldPos = mul(float4(vin.Pos, 1.0f), mWorld);
    vout.PosH = mul(float4(vin.Pos, 1.0f), mWorldViewProj);
    vout.WorldPos = worldPos.xyz;
    vout.Normal = normalize(mul(vin.Normal, (float3x3)mWorld));
    vout.TexC = vin.Tex * mUVTransform.xy + mUVTransform.zw;

    return vout;
}

PSOutput PS(VSOutput pin)
{
    PSOutput pout;

    // Диффузная текстура материала (как в лабораторной 1)
    float4 tex = gDiffuseMap.Sample(gSampler, pin.TexC);

    pout.Albedo = float4(tex.rgb, 1.0f);
    pout.Normal = float4(pin.Normal, 1.0f);
    pout.Depth = pin.PosH.z;

    // Бликовая составляющая: RGB - цвет блика, A - степень (0..1)
    pout.Specular = float4(gSpecularColor, saturate(gSpecularPower / 255.0f));

    return pout;
}
