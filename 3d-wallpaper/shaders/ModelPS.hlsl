// ModelPS.hlsl — Pixel shader for 3D models with PBR lighting

cbuffer CBMaterial : register(b0) {
    float4 baseColorFactor;
    float  metallicFactor;
    float  roughnessFactor;
    int    hasTexture;
    int    hasMetallicRoughnessMap;
    int    hasNormalMap;
    int    hasEmissiveMap;
    int    alphaMode;
    float  alphaCutoff;
    float3 emissiveFactor;
    int    emissiveTexCoord;
    float  clearcoatFactor;
    float  clearcoatRoughness;
    float2 paddingMat2;
};

struct CBLightDir {
    float4 color;     // rgb = color, a = intensity
    float4 direction; // xyz = dir
};

cbuffer CBLight : register(b1) {
    float4 ambientColor;
    CBLightDir lights[4];
    int numLights;
    int lightingEnabled;
    float exposure;
    float paddingLight;
};

Texture2D    diffuseTexture : register(t0);
Texture2D    mrTexture : register(t1);
Texture2D    normalTexture : register(t2);
Texture2D    emissiveTexture : register(t3);
SamplerState linearSampler  : register(s0);

struct PSInput {
    float4 position     : SV_POSITION;
    float2 texcoord     : TEXCOORD0;
    float3 normal       : NORMAL;
    float3 worldPos     : TEXCOORD2;
    float3x3 tbn        : TANGENT_MATRIX;
    float2 texcoord1    : TEXCOORD1;
};

static const float PI = 3.14159265359;

// GGX Normal Distribution Function
float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return num / max(denom, 0.0000001);
}

// Schlick-GGX Geometry function
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / denom;
}
float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

// Fresnel equation
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

static const float3x3 LINEAR_REC2020_TO_LINEAR_SRGB = float3x3(
    1.6605, -0.1246, -0.0182,
    -0.5876, 1.1329, -0.1006,
    -0.0728, -0.0083, 1.1187
);

static const float3x3 LINEAR_SRGB_TO_LINEAR_REC2020 = float3x3(
    0.6274, 0.0691, 0.0164,
    0.3293, 0.9195, 0.0880,
    0.0433, 0.0113, 0.8956
);

float3 agxDefaultContrastApprox(float3 x) {
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

float3 AgXToneMapping(float3 color) {
    static const float3x3 AgXInsetMatrix = float3x3(
        0.856627153315983, 0.137318972929847, 0.11189821299995,
        0.0951212405381588, 0.761241990602591, 0.0767994186031903,
        0.0482516061458583, 0.101439036467562, 0.811302368396859
    );

    static const float3x3 AgXOutsetMatrix = float3x3(
        1.1271005818144368, -0.1413297634984383, -0.14132976349843826,
        -0.11060664309660323, 1.157823702216272, -0.11060664309660294,
        -0.016493938717834573, -0.016493938717834257, 1.2519364065950405
    );

    const float AgxMinEv = -12.47393;
    const float AgxMaxEv = 4.026069;

    color = mul(color, LINEAR_SRGB_TO_LINEAR_REC2020);
    color = mul(color, AgXInsetMatrix);

    color = max(color, 1e-10);
    color = log2(color);
    color = (color - AgxMinEv) / (AgxMaxEv - AgxMinEv);
    color = saturate(color);

    color = agxDefaultContrastApprox(color);
    color = mul(color, AgXOutsetMatrix);
    
    color = pow(max(color, 0.0), 2.2);
    color = mul(color, LINEAR_REC2020_TO_LINEAR_SRGB);
    return saturate(color);
}

float4 PSMain(PSInput input) : SV_TARGET {
    // 1. Albedo
    // NOTE: diffuseTexture is SRGB format — GPU hardware automatically converts
    // to linear space during sampling. NO manual pow(2.2) needed.
    float4 albedo = baseColorFactor;
    if (hasTexture) {
        float4 texColor = diffuseTexture.Sample(linearSampler, input.texcoord);
        albedo *= texColor;
    }
    
    if (alphaMode == 1 && albedo.a < alphaCutoff) discard;
    
    if (lightingEnabled == 0) {
        float3 unlitColor = albedo.rgb;
        unlitColor = pow(abs(unlitColor), 1.0 / 2.2);
        return float4(unlitColor, albedo.a);
    }
    
    // 2. Normal Mapping
    float3 geomN = normalize(input.normal);
    float3 N = geomN;
    if (hasNormalMap) {
        float3 normalMap = normalTexture.Sample(linearSampler, input.texcoord).rgb;
        normalMap = normalMap * 2.0 - 1.0;
        N = normalize(mul(normalMap, input.tbn));
    }
    
    // 3. Metallic/Roughness (data texture — loaded as UNORM, no sRGB conversion)
    float metallic = metallicFactor;
    float roughness = roughnessFactor;
    if (hasMetallicRoughnessMap) {
        float4 mr = mrTexture.Sample(linearSampler, input.texcoord);
        roughness *= mr.g;
        metallic *= mr.b;
    }
    
    // Clamp metallic so base color texture is never fully suppressed
    float clampedMetallic = min(metallic, 0.65);
    
    // Camera at (0,0,+4) in our RH view matrix
    float3 cameraPos = float3(0.0, 0.0, 4.0);
    float3 V = normalize(cameraPos - input.worldPos);
    
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, albedo.rgb, metallic);
    
    float3 Lo = float3(0.0, 0.0, 0.0);
    
    // Direct lighting
    for (int i = 0; i < numLights; i++) {
        float3 L = normalize(-lights[i].direction.xyz);
        float3 H = normalize(V + L);
        
        float3 radiance = lights[i].color.rgb * lights[i].color.a;
        
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
        
        float3 kS = F;
        float3 kD = float3(1.0, 1.0, 1.0) - kS;
        kD *= (1.0 - clampedMetallic);
        
        float3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        float3 specular = numerator / denominator;
        
        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo.rgb / PI + specular) * radiance * NdotL;
    }
    
    // Hemisphere ambient light
    float3 skyColor = float3(0.15, 0.20, 0.25);
    float3 groundColor = float3(0.05, 0.05, 0.05);
    float hemisphereMix = N.y * 0.5 + 0.5;
    float3 fakeIrradiance = lerp(groundColor, skyColor, hemisphereMix);
    
    fakeIrradiance += ambientColor.rgb;
    
    float3 F_ambient = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    float3 kD_ambient = float3(1.0, 1.0, 1.0) - F_ambient;
    kD_ambient *= (1.0 - clampedMetallic);
    float3 ambientDiffuse = kD_ambient * albedo.rgb * fakeIrradiance;
    
    float3 R = reflect(-V, N);
    float envMix = R.y * 0.5 + 0.5;
    float3 envColor = lerp(groundColor, skyColor, envMix);
    float specIntensity = exp2(-roughness * 6.0);
    float3 ambientSpecular = F_ambient * envColor * specIntensity;

    float3 ambient = ambientDiffuse + ambientSpecular;
    
    // Emissive (SRGB format — hardware auto-decodes to linear)
    float3 emissive = emissiveFactor;
    if (hasEmissiveMap) {
        float2 eUv = (emissiveTexCoord == 1) ? input.texcoord1 : input.texcoord;
        emissive *= emissiveTexture.Sample(linearSampler, eUv).rgb;
    }
    ambient += emissive;

    float3 finalColor = Lo + ambient;
    finalColor *= exposure;
    
    // AgX Tone Mapping (matches modern Three.js default)
    finalColor = AgXToneMapping(finalColor);
    
    // Gamma correction (linear -> sRGB)
    finalColor = pow(abs(finalColor), 1.0 / 2.2);

    return float4(finalColor, albedo.a);
}
