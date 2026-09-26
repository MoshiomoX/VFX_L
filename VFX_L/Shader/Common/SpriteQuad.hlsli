// ============================================================
// SpriteQuad.hlsli
// One sprite-sheet frame drawn as a quad. Shared by the CPU Sprite entry
// renderer (VFXSpriteVS) and the GPU area sprites (SwarmSpriteVS).
// Must match VFXSpriteQuad in VFX_Editor/VFXSpriteRenderer.h (80 bytes).
// ============================================================
#ifndef SPRITE_QUAD_HLSLI
#define SPRITE_QUAD_HLSLI

#define SPRITE_FACING_BILLBOARD 0u // faces the camera
#define SPRITE_FACING_UPRIGHT   1u // stands up (world Y), turns only around Y
#define SPRITE_FACING_GROUND    2u // lies on the XZ plane, image top away from the camera

#define SPRITE_FLAG_ADDITIVE    1u // else alpha blended (premultiplied output)
// bits 8-15: texture array slice (GPU path only)

struct SpriteQuad
{
    float3 position; // world position of the pivot
    float rotation;  // radians, inside the quad plane
    float2 size;     // width, height in metres
    float2 pivot;    // pivot inside the frame, 0..1 from the top-left
    float4 uvRect;   // xy = top-left uv of the frame, zw = uv size
    float4 color;
    uint facing;
    uint flags;
    float2 _pad;
};

// Camera basis in world space (from the view matrix, shake included)
struct SpriteCamera
{
    float3 right;
    float3 up;
    float3 forward;
};

// vid 0..5 = two triangles
void SpriteCorner(SpriteQuad q, uint vid, SpriteCamera cam, out float3 world, out float2 uv)
{
    static const float2 k[6] =
    {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1),
    };
    float2 c = k[vid];
    uv = q.uvRect.xy + c * q.uvRect.zw;

    // offset from the pivot: x to the right, y up (image v grows downwards)
    float2 local = float2((c.x - q.pivot.x) * q.size.x, (q.pivot.y - c.y) * q.size.y);
    float s = sin(q.rotation);
    float co = cos(q.rotation);
    local = float2(local.x * co - local.y * s, local.x * s + local.y * co);

    float3 right = cam.right;
    float3 up = cam.up;
    if (q.facing != SPRITE_FACING_BILLBOARD)
    {
        float3 flatRight = float3(cam.right.x, 0.0, cam.right.z);
        float len = length(flatRight);
        right = (len > 1e-4) ? flatRight / len : float3(1, 0, 0);
        if (q.facing == SPRITE_FACING_UPRIGHT)
        {
            up = float3(0, 1, 0);
        }
        else
        {
            // ground: image top points away from the camera
            up = float3(-right.z, 0.0, right.x);
            if (dot(up, cam.forward) < 0.0)
                up = -up;
        }
    }
    world = q.position + right * local.x + up * local.y;
}

#endif
