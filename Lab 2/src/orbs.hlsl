// Отрисовка летящих лампочек — маленький густой комок света.
// Квадрат-билборд строится прямо в вершинном шейдере по SV_VertexID,
// вершинный буфер не нужен. Рисуется аддитивно (BLEND_ONE/ONE) после
// светового прохода, поэтому лампочка светится сама по себе.

cbuffer cbOrb : register(b0)
{
    float4x4 gViewProj;   // как и в остальных шейдерах лабы — транспонированная
    float3 gCamRight;  float gRadius;
    float3 gCamUp;     float gIntensity;
    float3 gCenter;    float gPad0;
    float3 gColor;     float gPad1;
};

struct OrbVSOut
{
    float4 PosH : SV_POSITION;
    float2 Local : TEXCOORD0;   // координаты в квадрате [-1, 1]
};

OrbVSOut VSOrb(uint vertexId : SV_VertexID)
{
    // Два треугольника, покрывающие квадрат [-1, 1] в плоскости, повёрнутой к камере.
    const float2 corners[6] =
    {
        float2(-1.0f, -1.0f), float2(-1.0f, 1.0f), float2(1.0f, -1.0f),
        float2(1.0f, -1.0f),  float2(-1.0f, 1.0f), float2(1.0f, 1.0f)
    };

    float2 c = corners[vertexId];

    // Центр шара + смещение по осям камеры — получается шар, всегда смотрящий на камеру.
    float3 worldPos = gCenter + gCamRight * (c.x * gRadius) + gCamUp * (c.y * gRadius);

    OrbVSOut o;
    o.PosH = mul(float4(worldPos, 1.0f), gViewProj);
    o.Local = c;
    return o;
}

float4 PSOrb(OrbVSOut pin) : SV_Target
{
    float r = length(pin.Local);

    if (r >= 1.0f)
        discard;

    // Густое ядро: почти весь шар горит ровно, гаснет только у самой кромки.
    float d = 1.0f - r;
    float core = smoothstep(0.25f, 0.75f, d);
    float halo = pow(saturate(d), 5.0f) * 0.18f;

    float v = core + halo;

    // Аддитивное смешивание: цвет просто прибавляется к кадру.
    return float4(gColor * (v * gIntensity), v);
}
