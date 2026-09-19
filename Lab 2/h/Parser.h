#pragma once

#include <string>
#include <vector>
#include "Submesh.h"
#include "Vertex.h"

bool LoadOBJ(
    const std::string& filename,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices,
    std::vector<Submesh>& outSubmeshes);

struct ParsedMaterial
{
    std::string Name;
    std::string DiffuseMap;   // Первая текстура (map_Kd)
    std::string DiffuseMap2;  // Вторая текстура (map_Kd2)
    DirectX::XMFLOAT3 Kd = { 1.0f, 1.0f, 1.0f };
};

bool LoadMTL(
    const std::string& filename,
    std::vector<ParsedMaterial>& materials);