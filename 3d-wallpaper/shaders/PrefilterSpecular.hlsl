// PrefilterSpecular.hlsl — Compute shader to generate prefiltered specular cubemap
// GGX importance sampling per mip level for split-sum IBL approximation
// Dispatch: (mipSize/8, mipSize/8, 6) per mip level

cbuffer CBPrefilter : register(b0) {
    uint mipSize;       // Size of current mip level face
    float roughness;    // Roughness for this mip level
    uint totalMips;     // Total number of mip levels
    uint sourceFaceSize; // Size of the source environment cubemap face
};

TextureCube<float4> envMap : register(t0);
RWTexture2DArray<float4> prefilteredMap : register(u0);
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

// Radical inverse for Hammersley sequence (Van der Corput)
float RadicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

float2 Hammersley(uint i, uint N) {
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

// GGX importance sampling
float3 ImportanceSampleGGX(float2 Xi, float3 N, float a) {
    float a2 = a * a;
    
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a2 * a2 - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    
    // Spherical to cartesian (tangent space)
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;
    
    // Tangent space to world
    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= mipSize || dtid.y >= mipSize) return;
    
    uint face = dtid.z;
    float2 uv = (float2(dtid.xy) + 0.5) / float(mipSize);
    float3 N = CubeDirection(face, uv);
    
    // Assume view direction = normal direction (split-sum approximation)
    float3 R = N;
    float3 V = R;
    
    float a = roughness;
    
    // For roughness = 0, just sample the environment directly
    if (roughness < 0.001) {
        float4 color = envMap.SampleLevel(linearSampler, N, 0);
        prefilteredMap[dtid] = color;
        return;
    }
    
    const uint SAMPLE_COUNT = 1024;
    float3 prefilteredColor = float3(0, 0, 0);
    float totalWeight = 0.0;
    
    for (uint i = 0; i < SAMPLE_COUNT; i++) {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(Xi, N, a);
        float3 L = normalize(2.0 * dot(V, H) * H - V);
        
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            // Compute the mip level to sample from to reduce aliasing
            float NdotH = max(dot(N, H), 0.0);
            float HdotV = max(dot(H, V), 0.0);
            float a2 = a * a;
            float D = a2 * a2 / (PI * pow(NdotH * NdotH * (a2 * a2 - 1.0) + 1.0, 2.0));
            float pdf = D * NdotH / (4.0 * HdotV) + 0.0001;
            
            float saTexel = 4.0 * PI / (6.0 * sourceFaceSize * sourceFaceSize);
            float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 0.0001);
            float mipLevel = (roughness == 0.0) ? 0.0 : 0.5 * log2(saSample / saTexel);
            
            prefilteredColor += envMap.SampleLevel(linearSampler, L, mipLevel).rgb * NdotL;
            totalWeight += NdotL;
        }
    }
    
    prefilteredColor /= max(totalWeight, 0.001);
    prefilteredMap[dtid] = float4(prefilteredColor, 1.0);
}
