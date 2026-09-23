// ============================================
// EmitMesh.hlsli
// Emit from a mesh that already lives on the GPU.
// The source is a raw vertex buffer (ByteAddressBuffer); the layout
// tells where position / normal sit inside one vertex.
//
//   triangleCount > 0 : pick a random triangle from the raw index
//                       buffer, then a random point on it (uniform
//                       barycentric). Faces without vertices (capsule
//                       cylinder) get particles too. Not area weighted.
//   triangleCount == 0: pick a random vertex.
//   edgeMode == 1     : pick only from edgeIndices (vertices on the
//                       dissolve edge, built by EdgeFilterCS).
//                       edgeCount == 0 -> nothing to emit, 'emitted'
//                       comes back false and the caller kills the particle.
//
// Position is taken to world space with e.world, then e.position is
// added as an extra offset (effect worldOffset etc.).
// ============================================

#ifndef EMIT_MESH_HLSLI
#define EMIT_MESH_HLSLI

uint LoadMeshIndex(ByteAddressBuffer ib, uint indexBytes, uint i)
{
    if (indexBytes == 2u)
    {
        uint word = ib.Load((i >> 1u) << 2u);
        return (i & 1u) ? (word >> 16u) : (word & 0xFFFFu);
    }
    return ib.Load(i << 2u);
}

void EmitMesh(GPUEmitter e, inout uint seed,
              ByteAddressBuffer src, ByteAddressBuffer ib, EmitSourceLayout L,
              StructuredBuffer<uint> edgeIndices, uint edgeCount,
              out float3 pos, out float3 vel, out bool emitted)
{
    pos = e.position;
    vel = float3(0, 0, 0);
    emitted = false;

    if (e.sourceCount <= 0 || L.stride == 0u)
        return;

    float3 p;
    float3 n;

    if (e.edgeMode == 0 && L.triangleCount > 0u)
    {
        // ---- triangle: random face, uniform point on it ----
        uint t = (uint) (Random(seed) * (float) L.triangleCount);
        t = min(t, L.triangleCount - 1u);

        uint i0 = LoadMeshIndex(ib, L.indexBytes, t * 3u + 0u);
        uint i1 = LoadMeshIndex(ib, L.indexBytes, t * 3u + 1u);
        uint i2 = LoadMeshIndex(ib, L.indexBytes, t * 3u + 2u);
        uint vmax = (uint) (e.sourceCount - 1);
        i0 = min(i0, vmax);
        i1 = min(i1, vmax);
        i2 = min(i2, vmax);

        float3 p0 = asfloat(src.Load3(i0 * L.stride + L.posOffset));
        float3 p1 = asfloat(src.Load3(i1 * L.stride + L.posOffset));
        float3 p2 = asfloat(src.Load3(i2 * L.stride + L.posOffset));
        float3 n0 = asfloat(src.Load3(i0 * L.stride + L.normalOffset));
        float3 n1 = asfloat(src.Load3(i1 * L.stride + L.normalOffset));
        float3 n2 = asfloat(src.Load3(i2 * L.stride + L.normalOffset));

        float r1 = Random(seed);
        float r2 = Random(seed);
        if (r1 + r2 > 1.0)
        {
            r1 = 1.0 - r1;
            r2 = 1.0 - r2;
        }
        float r0 = 1.0 - r1 - r2;

        p = p0 * r0 + p1 * r1 + p2 * r2;
        n = n0 * r0 + n1 * r1 + n2 * r2;
    }
    else
    {
        // ---- vertex: all vertices, or the dissolve-edge table ----
        uint idx;
        if (e.edgeMode == 1)
        {
            if (edgeCount == 0u)
                return;
            uint k = (uint) (Random(seed) * (float) edgeCount);
            k = min(k, edgeCount - 1u);
            idx = edgeIndices[k];
        }
        else
        {
            idx = (uint) (Random(seed) * (float) e.sourceCount);
        }
        idx = min(idx, (uint) (e.sourceCount - 1));
        uint base = idx * L.stride;

        p = asfloat(src.Load3(base + L.posOffset));
        n = asfloat(src.Load3(base + L.normalOffset));
    }

    pos = mul(float4(p, 1.0), e.world).xyz + e.position;

    // velocity along the (rotated) normal
    float3 dir = mul(n, (float3x3) e.world);
    float len = length(dir);
    dir = (len > 1e-6) ? dir / len : float3(0, 1, 0);

    float speed = RandomRange(seed, e.speedRange.x, e.speedRange.y);
    vel = dir * speed;
    emitted = true;
}

#endif
