#pragma once
#include <SimpleMath.h>


struct VERTEX_3D
{
    DirectX::SimpleMath::Vector3 position;
    DirectX::SimpleMath::Vector3 normal;
    DirectX::SimpleMath::Vector2 uv;
    DirectX::SimpleMath::Vector4 color;
};