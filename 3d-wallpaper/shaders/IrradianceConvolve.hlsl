// IrradianceConvolve.hlsl — Compute shader to convolve a cubemap into an irradiance map
// Cosine-weighted hemisphere integration for diffuse ambient lighting
// Dispatch: (irradianceSize/8, irradianceSize/8, 6)

cbuffer CBIrradiance : register(b0) {
    uint irradianceSize;
    uint3 pad;
};

TextureCube<float4> envMap : register(t0);
RWTexture2DArray<float4> irradianceMap : register(u0);
SamplerState linearSampler : register(s0);

static const float PI = 3.14159265359;

float3 CubeDirection(uint face, float2 uv) {
    float u = uv.x * 2.0 - 1.0;
    float v = uv.y * 2.0 - 1.0;
    
    float3 dir = float3(1.0, 0.0, 0.0);
    switch (face) {
        case 0: dir = float3( 1.0,   -v,   -u); break;
        case 1: dir = float3(-1.0,   -v,    u); break;
        case 2: dir = float3(   u,  1.0,    v); break;
        case 3: dir = float3(   u, -1.0,   -v); break;
        case 4: dir = float3(   u,   -v,  1.0); break;
        case 5: dir = float3(  -u,   -v, -1.0); break;
        default: break;
    }
    return normalize(dir);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= irradianceSize || dtid.y >= irradianceSize) return;
    
    uint face = dtid.z;
    float2 uv = (float2(dtid.xy) + 0.5) / float(irradianceSize);
    float3 N = CubeDirection(face, uv);
    
    // Build a tangent frame from N
    float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 right = normalize(cross(up, N));
    up = cross(N, right);
    
    // Monte Carlo cosine-weighted hemisphere sampling
    float3 irradiance = float3(0, 0, 0);
    
    // Use uniform grid sampling over hemisphere for deterministic results
    const uint SAMPLE_COUNT_PHI = 64;
    const uint SAMPLE_COUNT_THETA = 16;
    float sampleDelta = 1.0;
    uint nrSamples = 0;
    
    for (uint iPhi = 0; iPhi < SAMPLE_COUNT_PHI; iPhi++) {
        float phi = (2.0 * PI * iPhi) / float(SAMPLE_COUNT_PHI);
        for (uint iTheta = 0; iTheta < SAMPLE_COUNT_THETA; iTheta++) {
            float theta = (0.5 * PI * iTheta) / float(SAMPLE_COUNT_THETA);
            
            // Spherical to cartesian (in tangent space)
            float3 tangentSample = float3(
                sin(theta) * cos(phi),
                sin(theta) * sin(phi),
                cos(theta)
            );
            
            // Tangent space to world
            float3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;
            
            // cos(theta) weighting * sin(theta) (solid angle correction)
            irradiance += envMap.SampleLevel(linearSampler, sampleVec, 0).rgb * cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    
    irradiance = PI * irradiance / float(nrSamples);
    
    irradianceMap[dtid] = float4(irradiance, 1.0);
}
