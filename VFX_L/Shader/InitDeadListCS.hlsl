cbuffer DeadListCB : register(b0)
{
    uint deadCount;
    uint maxParticles;
    uint _pad0;
    uint _pad1;
};

AppendStructuredBuffer<uint> deadList : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= maxParticles)
        return;

    deadList.Append(id.x); 
}