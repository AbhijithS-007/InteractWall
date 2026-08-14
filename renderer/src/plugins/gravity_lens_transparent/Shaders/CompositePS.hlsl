Texture2D BaseTex : register(t0);
Texture2D DispTex : register(t1);
SamplerState Sampler : register(s0);

cbuffer CompositeCB : register(b0) {
    float dispersion;
    float coreDarkening;
    int enableFX;
    float aspectRatio;
    float2 cursorUV;
    float pressRadius;
    float shadingStrength;  // 0 = no shading, 1 = full directional shading
}

struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float4 main(VS_OUTPUT input) : SV_TARGET {
    // Sample displacement field (bilinear upscaled from low-res grid)
    float2 disp = DispTex.SampleLevel(Sampler, input.uv, 0).xy;
    
    // With +dir displacement (toward cursor), subtracting pushes sample
    // point FURTHER from cursor → demagnification → concave/receding look
    float2 baseUV = input.uv - disp;
    baseUV = saturate(baseUV);

    float dispMag = length(disp);
    
    float4 color = float4(0,0,0,1);
    
    // Chromatic dispersion (optional FX)
    if (enableFX > 0 && dispersion > 0.0) {
        float2 dispOffset = disp * dispersion;
        color.r = BaseTex.SampleLevel(Sampler, saturate(baseUV - dispOffset), 0).r;
        color.g = BaseTex.SampleLevel(Sampler, baseUV, 0).g;
        color.b = BaseTex.SampleLevel(Sampler, saturate(baseUV + dispOffset), 0).b;
    } else {
        color.rgb = BaseTex.SampleLevel(Sampler, baseUV, 0).rgb;
    }
    
    // Core darkening (subtle vignette at cursor center — dent is deeper here)
    if (enableFX > 0 && coreDarkening > 0.0) {
        float2 distVec = input.uv - cursorUV;
        distVec.x *= aspectRatio;
        float dist = length(distVec);
        float darken = smoothstep(pressRadius, 0.0, dist) * coreDarkening;
        color.rgb *= (1.0 - darken);
    }
    
    // =========================================================
    // FAKE-NORMAL DIRECTIONAL SHADING PASS
    // Derives a fake surface normal from the displacement field's
    // local gradient, then applies simple directional lighting to
    // create the visual illusion of a concave dent.
    // =========================================================
    if (shadingStrength > 0.001 && dispMag > 0.0001) {
        // Sample displacement magnitude at neighboring grid cells
        // to compute the gradient (slope) of the "depth" surface.
        // texStep matches the displacement grid resolution (128x128).
        float texStep = 1.0 / 128.0;
        
        float magR = length(DispTex.SampleLevel(Sampler, input.uv + float2(texStep, 0), 0).xy);
        float magL = length(DispTex.SampleLevel(Sampler, input.uv - float2(texStep, 0), 0).xy);
        float magD = length(DispTex.SampleLevel(Sampler, input.uv + float2(0, texStep), 0).xy);
        float magU = length(DispTex.SampleLevel(Sampler, input.uv - float2(0, texStep), 0).xy);
        
        // Gradient of displacement magnitude.
        // Higher displacement magnitude = surface pulled further toward cursor
        //   = deeper into the dent = LOWER surface height.
        // Negate so the "height" gradient points uphill (from deep center
        // toward the flat rim), giving correct concave normals.
        float dX = magL - magR;  // negated X gradient
        float dY = magU - magD;  // negated Y gradient (UV Y is screen-down)
        
        // Construct fake surface normal from the height gradient.
        // normalScale controls how steep the surface appears.
        float normalScale = 60.0;
        float3 fakeNormal = normalize(float3(
            dX * normalScale,
            dY * normalScale,
            1.0
        ));
        
        // Fixed light direction: from upper-left of screen.
        // In our coordinate system: +X = right, +Y = up (screen), +Z = toward viewer.
        float3 lightDir = normalize(float3(0.6, 0.5, 1.0));
        
        // Half-Lambert shading for a softer, less harsh look
        float NdotL = dot(fakeNormal, lightDir);
        float halfLambert = NdotL * 0.5 + 0.5;
        
        // Only apply shading where there's actual displacement (inside the dent)
        float shadeMask = smoothstep(0.0, 0.003, dispMag);
        float shade = lerp(1.0, halfLambert, shadeMask * shadingStrength);
        
        color.rgb *= shade;
    }
    
    return color;
}
