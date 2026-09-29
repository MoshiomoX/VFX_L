// ============================================================
// SwarmSprite.hlsli
// Sprite-sheet animations started by GPU areas (projectile hits).
// SwarmSpriteCS starts one instance per Sprite entry of the area's recipe
// when a new area shows up in a slot, ages the pool and retires finished
// instances; SwarmSpriteVS/PS draw every live instance.
// The instances live in their own ring pool, so an animation keeps
// playing after its (short) area is gone.
// Must match Swarm::VFXSpriteDef / Swarm::SpriteInstance in SwarmVFXTable.h
// ============================================================
#ifndef SWARM_SPRITE_HLSLI
#define SWARM_SPRITE_HLSLI

#define SWARM_SPRITE_ADDITIVE 1u // same bit as SPRITE_FLAG_ADDITIVE
#define SWARM_SPRITE_LOOP     2u
// bits 8-15: slice of the sprite texture array

// One Sprite entry of a recipe (80 bytes)
struct SwarmSpriteDef
{
    float3 offset;   // from the area centre
    float height;    // metres
    float4 color;
    float2 cellUV;   // frame size in uv of the array slice
    float2 pivot;    // inside the frame, 0..1 from the top-left
    uint cols;
    uint frameCount;
    float frameTime; // seconds per frame, playback speed applied
    float life;      // seconds an instance lives
    uint facing;     // SPRITE_FACING_*
    uint flags;
    float rotation;  // radians
    float aspect;    // width / height
};

// One live animation (32 bytes)
struct SwarmSprite
{
    float3 position;
    float age;
    uint def;
    uint alive;
    float sizeScale; // the area's size scale (Magnifier). 0 = 1
    uint _pad;
};

#endif
