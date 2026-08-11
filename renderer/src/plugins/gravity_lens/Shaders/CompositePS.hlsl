Texture2D BaseTex : register(t0);
Texture2D DispTex : register(t1);
SamplerState Sampler : register(s0);

cbuffer CompositeCB : register(b0) {
    float dispersion;
    float coreDarkening;
    int enableFX;
    float aspectRatio;
    float2 cursorUV;
    float lensRadius;
    float padding2;
}

struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float4 main(VS_OUTPUT input) : SV_TARGET {
    // Sample displacement field (bilinear upscaled)
    float2 disp = DispTex.SampleLevel(Sampler, input.uv, 0).xy;
    
    float2 baseUV = input.uv - disp; // Pulls pixels toward cursor
    baseUV = saturate(baseUV); // Clamp to prevent edge bleeding

    float dispMag = length(disp);
    
    float4 color = float4(0,0,0,1);
    
    if (enableFX > 0 && dispersion > 0.0) {
        float2 dispOffset = disp * dispersion;
        color.r = BaseTex.SampleLevel(Sampler, saturate(baseUV - dispOffset), 0).r;
        color.g = BaseTex.SampleLevel(Sampler, baseUV, 0).g;
        color.b = BaseTex.SampleLevel(Sampler, saturate(baseUV + dispOffset), 0).b;
    } else {
        color.rgb = BaseTex.SampleLevel(Sampler, baseUV, 0).rgb;
    }
    
    if (enableFX > 0 && coreDarkening > 0.0) {
        // Darken based on distance to cursor to form a perfect circular vignette
        float2 distVec = input.uv - cursorUV;
        distVec.x *= aspectRatio;
        float dist = length(distVec);
        float darken = smoothstep(lensRadius, 0.0, dist) * coreDarkening;
        color.rgb *= (1.0 - darken);
    }
    
    return color;
}
