Texture2D gAlbedoMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gDepthMap : register(t2);
Texture2D gSpecularMap : register(t3);
SamplerState gSampler : register(s0);

cbuffer cbLighting : register(b0)
{
    float3 gLightPos;
    float gLightIntensity;
    float3 gLightColor;
    float gLightRange;
    float3 gLightDir;
    float gSpotAngle;
    float3 gAmbientColor;
    int gLightType;
    float3 gCameraPos;
    float padding;
};

cbuffer cbCamera : register(b1)
{
    float4x4 mInvViewProj;
    float3 mCameraPos;
    int mDebugMode;
    float2 mScreenSize;
    float2 padding2;
};

static const int LIGHT_AMBIENT = 0;
static const int LIGHT_DIRECTIONAL = 1;
static const int LIGHT_POINT = 2;
static const int LIGHT_SPOT = 3;

struct VSInput
{
    uint vertexId : SV_VertexID;
};

struct PSInput
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

float3 ReconstructWorldPos(float2 texCoord, float depth, float4x4 invViewProj)
{
    float x = texCoord.x * 2.0f - 1.0f;
    float y = (1.0f - texCoord.y) * 2.0f - 1.0f;

    float4 clipPos = float4(x, y, depth, 1.0f);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

// Слагаемое Блинна-Фонга: диффуз + блик
float3 ComputeLight(float3 lightDir, float3 normal, float3 viewDir,
                    float3 albedo, float3 specularColor, float specularPower,
                    float3 lightColor, float intensity, float attenuation)
{
    float diff = max(dot(normal, lightDir), 0.0f);

    float3 halfway = normalize(lightDir + viewDir);
    float spec = 0.0f;
    if (diff > 0.0f)
    {
        spec = pow(max(dot(normal, halfway), 0.0f), max(specularPower, 1.0f));
    }

    float3 diffuse = diff * albedo;
    float3 specular = spec * specularColor;

    return (diffuse + specular) * lightColor * intensity * attenuation;
}

PSInput VS(VSInput vin)
{
    PSInput vout;
    float2 texCoord = float2((vin.vertexId << 1) & 2, vin.vertexId & 2);
    vout.TexC = texCoord;
    vout.PosH = float4(texCoord.x * 2.0f - 1.0f, -(texCoord.y * 2.0f - 1.0f), 0.0f, 1.0f);
    return vout;
}

float4 PS(PSInput pin) : SV_Target
{
    float4 albedo = gAlbedoMap.Sample(gSampler, pin.TexC);
    float4 normalData = gNormalMap.Sample(gSampler, pin.TexC);
    float depth = gDepthMap.Sample(gSampler, pin.TexC).r;
    float4 specularData = gSpecularMap.Sample(gSampler, pin.TexC);

    // Отладочный вывод слоёв G-буфера (клавиши 0..4)
    if (mDebugMode == 1)
        return float4(albedo.rgb, 1.0f);
    if (mDebugMode == 2)
        return float4(normalData.xyz * 0.5f + 0.5f, 1.0f);
    if (mDebugMode == 3)
        return float4(depth, depth, depth, 1.0f);
    if (mDebugMode == 4)
        return float4(specularData.rgb, 1.0f);

    float3 worldPos = ReconstructWorldPos(pin.TexC, depth, mInvViewProj);
    float3 normal = normalize(normalData.xyz);
    float3 viewDir = normalize(mCameraPos - worldPos);

    if (depth > 0.99999f)
        return float4(0, 0, 0, 0);

    float3 specularColor = specularData.rgb;
    float specularPower = max(specularData.a * 255.0f, 1.0f);

    float3 result = float3(0, 0, 0);

    if (gLightType == LIGHT_AMBIENT)
    {
        result = albedo.rgb * gAmbientColor;
    }
    if (gLightType == LIGHT_DIRECTIONAL)
    {
        float3 lightDir = normalize(-gLightDir);
        result = ComputeLight(lightDir, normal, viewDir, albedo.rgb,
                              specularColor, specularPower,
                              gLightColor, gLightIntensity, 1.0f);
    }
    if (gLightType == LIGHT_POINT)
    {
        float3 lightDir = gLightPos - worldPos;
        float distance = length(lightDir);
        lightDir = normalize(lightDir);

        float attenuation = 1.0f - saturate(distance / gLightRange);
        attenuation = attenuation * attenuation;

        result = ComputeLight(lightDir, normal, viewDir, albedo.rgb,
                              specularColor, specularPower,
                              gLightColor, gLightIntensity, attenuation);
    }
    if (gLightType == LIGHT_SPOT)
    {
        float3 lightDir = gLightPos - worldPos;
        float distance = length(lightDir);
        lightDir = normalize(lightDir);

        float3 spotDir = normalize(gLightDir);
        float cosAngle = dot(lightDir, spotDir);
        float cosCone = cos(gSpotAngle / 2.0f);

        if (cosAngle > cosCone)
        {
            float attenuation = 1.0f - saturate(distance / gLightRange);
            attenuation = attenuation * attenuation;

            float spotFactor = saturate((cosAngle - cosCone) / (1.0f - cosCone));

            result = ComputeLight(lightDir, normal, viewDir, albedo.rgb,
                                  specularColor, specularPower,
                                  gLightColor, gLightIntensity, attenuation * spotFactor);
        }
    }

    return float4(result, 0.0f);
}
