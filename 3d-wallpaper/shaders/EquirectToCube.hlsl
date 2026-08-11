// EquirectToCube.hlsl — Compute shader to convert equirectangular HDR to cubemap
// Dispatch: (faceSize/8, faceSize/8, 6) — one thread per texel per face

cbuffer CBConvert : register(b0) {
    uint faceSize;
    uint3 pad;
};

Texture2D<float4> equirectMap : register(t0);
RWTexture2DArray<float4> cubeMap : register(u0);
SamplerState linearSampler : register(s0);

static const float PI = 3.14159265359;

// Convert cubemap face + UV to 3D direction vector
float3 CubeDirection(uint face, float2 uv) {
    // Map [0,1] UV to [-1,1]
    float u = uv.x * 2.0 - 1.0;
    float v = uv.y * 2.0 - 1.0;
    
    float3 dir = float3(1.0, 0.0, 0.0);
    switch (face) {
        case 0: dir = float3( 1.0,   -v,   -u); break; // +X
        case 1: dir = float3(-1.0,   -v,    u); break; // -X
        case 2: dir = float3(   u,  1.0,    v); break; // +Y
        case 3: dir = float3(   u, -1.0,   -v); break; // -Y
        case 4: dir = float3(   u,   -v,  1.0); break; // +Z
        case 5: dir = float3(  -u,   -v, -1.0); break; // -Z
        default: break;
    }
    return normalize(dir);
}

// Convert 3D direction to equirectangular UV
float2 DirToEquirect(float3 dir) {
    float phi = atan2(dir.z, dir.x);       // [-PI, PI]
    float theta = asin(clamp(dir.y, -1.0, 1.0)); // [-PI/2, PI/2]
    
    float u = phi / (2.0 * PI) + 0.5;      // [0, 1]
    float v = -theta / PI + 0.5;            // [0, 1]
    return float2(u, v);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= faceSize || dtid.y >= faceSize) return;
    
    uint face = dtid.z;
    float2 uv = (float2(dtid.xy) + 0.5) / float(faceSize);
    
    float3 dir = CubeDirection(face, uv);
    float2 equirectUV = DirToEquirect(dir);
    
    float4 color = equirectMap.SampleLevel(linearSampler, equirectUV, 0);
    cubeMap[dtid] = color;
}
