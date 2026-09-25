#pragma once
#include <DirectXMath.h>
#include <vector>

using namespace DirectX;

enum LightType
{
    LIGHT_AMBIENT = 0,
    LIGHT_DIRECTIONAL = 1,
    LIGHT_POINT = 2,
    LIGHT_SPOT = 3,
};

struct Light
{
    LightType Type = LIGHT_POINT;

    // Общие параметры
    XMFLOAT3 Position = XMFLOAT3(0, 0, 0);
    XMFLOAT3 Color = XMFLOAT3(1, 1, 1);
    float Intensity = 1.0f;

    // Для directional и spot
    XMFLOAT3 Direction = XMFLOAT3(0, -1, 0);

    // Для point и spot
    float Range = 10.0f;

    // Для spot
    float SpotAngle = XM_PIDIV4;
    float SpotFalloff = 1.0f;

    // Для ambient
    XMFLOAT3 AmbientColor = XMFLOAT3(0.2f, 0.2f, 0.2f);

    Light() = default;

    static Light CreateAmbientLight(const XMFLOAT3& color)
    {
        Light light;
        light.Type = LIGHT_AMBIENT;
        light.AmbientColor = color;
        return light;
    }

    static Light CreateDirectionalLight(const XMFLOAT3& dir, const XMFLOAT3& color, float intensity)
    {
        Light light;
        light.Type = LIGHT_DIRECTIONAL;
        light.Direction = dir;
        light.Color = color;
        light.Intensity = intensity;
        return light;
    }

    static Light CreatePointLight(const XMFLOAT3& pos, const XMFLOAT3& color, float intensity, float range)
    {
        Light light;
        light.Type = LIGHT_POINT;
        light.Position = pos;
        light.Color = color;
        light.Intensity = intensity;
        light.Range = range;
        return light;
    }

    static Light CreateSpotLight(const XMFLOAT3& pos, const XMFLOAT3& dir, const XMFLOAT3& color,
                                  float intensity, float range, float angle)
    {
        Light light;
        light.Type = LIGHT_SPOT;
        light.Position = pos;
        light.Direction = dir;
        light.Color = color;
        light.Intensity = intensity;
        light.Range = range;
        light.SpotAngle = angle;
        return light;
    }
};

// ===== Летящие лампочки (клавиша L) =====

// Настройки лампочек собраны в одном месте, чтобы их было просто менять.
namespace BulbSettings
{
    // Parser.cpp масштабирует Sponza (OBJ_SCALE = 0.01) и центрирует её по
    // габаритам, поэтому единицы сцены — метры: коробка модели 37.2 x 15.6 x 22.9.
    const float BelowCameraOffset = 0.40f;  // на сколько ниже камеры рождается лампочка
    const float Radius = 0.05f;             // радиус светового шара лампочки
    const float CoreIntensity = 2.2f;       // яркость ядра шара
    const float Color[3] = { 1.0f, 0.93f, 0.78f };  // тёплый свет лампочки
    const float Speed = 4.0f;               // скорость полёта, единиц в секунду
    const float Interval = 0.8f;            // раз в сколько секунд рождается новая лампочка
    const float LightIntensity = 0.7f;      // яркость лампочки как источника света сцены
    const float LightRange = 4.0f;          // радиус, на который лампочка освещает сцену
    const float FallbackDistance = 25.0f;   // если луч не задел модель — лампочка улетает и гаснет
}

// «Лампочка» — маленький светящийся шар, который вылетает из-под камеры.
// Летит строго прямо: направление задаётся один раз при рождении и не меняется.
// MaxDistance — расстояние до первой поверхности Sponza: пролетев его, лампочка
// мгновенно исчезает (никакого затухания).
struct FlyingBulb
{
    XMFLOAT3 Position = XMFLOAT3(0, 0, 0);
    XMFLOAT3 Direction = XMFLOAT3(0, 0, 1);
    float Speed = BulbSettings::Speed;
    float Distance = 0.0f;      // сколько уже пролетела
    float MaxDistance = 10.0f;  // сколько ей отведено до касания модели
};

// Структура для Constant Buffer
struct LightConstants
{
    XMFLOAT3 LightPos;
    float LightIntensity;
    XMFLOAT3 LightColor;
    float LightRange;
    XMFLOAT3 LightDir;
    float SpotAngle;
    XMFLOAT3 AmbientColor;
    int LightType;
    XMFLOAT3 CameraPos;
    float Padding;

    LightConstants()
    {
        LightPos = XMFLOAT3(0, 0, 0);
        LightIntensity = 1.0f;
        LightColor = XMFLOAT3(1, 1, 1);
        LightRange = 10.0f;
        LightDir = XMFLOAT3(0, -1, 0);
        SpotAngle = XM_PIDIV4;
        AmbientColor = XMFLOAT3(0.2f, 0.2f, 0.2f);
        LightType = 0;
        CameraPos = XMFLOAT3(0, 0, 0);
        Padding = 0;
    }

    void SetFromLight(const Light& light, const XMFLOAT3& cameraPos)
    {
        LightPos = light.Position;
        LightIntensity = light.Intensity;
        LightColor = light.Color;
        LightRange = light.Range;
        LightDir = light.Direction;
        SpotAngle = light.SpotAngle;
        AmbientColor = light.AmbientColor;
        LightType = light.Type;
        CameraPos = cameraPos;
    }
};
