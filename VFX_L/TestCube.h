#pragma once
#include <vector>
#include "Vertex3D.h"

class TestCube
{
public:
    static std::vector<VERTEX_3D> GetVertices();
    static std::vector<unsigned int> GetIndices();
};