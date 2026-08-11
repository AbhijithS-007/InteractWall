Texture2D txBase : register(t0);
Texture2D txMask : register(t1);
SamplerState samLinear : register(s0);

cbuffer DropletCB : register(b0) {
    float screenWidth;
    float screenHeight;
    int qualityTier;
    float padding;
}

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 screenPos : TEXCOORD0;
    float2 center : TEXCOORD1;
    float radius : COLOR0;
    float alpha : COLOR1;
    float trail : COLOR2;
    float seed : COLOR3;
};

float hash(float2 p) { return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453); }
float noise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    f = f*f*(3.0-2.0*f);
    return lerp(lerp(hash(i), hash(i + float2(1,0)), f.x),
                lerp(hash(i + float2(0,1)), hash(i + float2(1,1)), f.x), f.y);
}
float fbm(float2 p) {
    float f = 0.5000 * noise(p); p *= 2.02;
    f += 0.2500 * noise(p); p *= 2.03;
    f += 0.1250 * noise(p);
    return f;
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 texcoord = input.screenPos / float2(screenWidth, screenHeight);
    float mask = txMask.SampleLevel(samLinear, texcoord, 0).r;
    if (mask > 0.5) discard;
    
    float absoluteY = input.screenPos.y / screenHeight;
    float centerAbsoluteY = input.center.y / screenHeight;
    
    float meander = 0.0;
    float centerMeander = 0.0;
    
    if (qualityTier > 0) {
        meander = noise(float2(absoluteY * 5.0, input.seed * 10.0)) * 0.05 * screenWidth;
        centerMeander = noise(float2(centerAbsoluteY * 5.0, input.seed * 10.0)) * 0.05 * screenWidth;
    }
    
    float2 warpedScreenPos = input.screenPos;
    warpedScreenPos.x -= meander;
    
    float2 warpedCenter = input.center;
    warpedCenter.x -= centerMeander;
    
    float elongation = (qualityTier > 0) ? 1.0 + (input.radius * 0.05) * input.seed : 1.0;
    float2 diff = warpedScreenPos - warpedCenter;
    float2 stretchedDiff = diff;
    stretchedDiff.y /= elongation;
    float distToCenter = length(stretchedDiff);
    
    float2 pa = warpedScreenPos - (warpedCenter - float2(0, input.trail));
    float2 ba = float2(0, input.trail);
    float h = saturate(dot(pa, ba) / max(dot(ba, ba), 1e-5));
    float distToTrail = length(pa - ba * h);
    
    float softEdge = (qualityTier > 0) ? 1.5 + input.seed * 1.5 : 1.5;
    
    float dropAlpha = 1.0 - smoothstep(max(0.0, input.radius - softEdge), input.radius, distToCenter);
    
    float trailAlpha = 0.0;
    if (input.trail > 0.1) {
        trailAlpha = 1.0 - smoothstep(max(0.0, input.radius * 0.5 - softEdge), input.radius * 0.5, distToTrail);
        float wetness = 1.0 - saturate(pa.y / max(input.trail, 1.0));
        trailAlpha *= wetness * 0.5; 
    }
    
    float shapeAlpha = max(dropAlpha, trailAlpha);
    if (shapeAlpha <= 0.0) discard;
    
    float4 finalColor = float4(0,0,0,0);
    
    if (distToCenter <= input.radius + 0.5) {
        float z = sqrt(max(0.001, input.radius*input.radius - distToCenter*distToCenter));
        float3 normal = normalize(float3(stretchedDiff.x, stretchedDiff.y, z));
        float2 refracTex = texcoord + normal.xy * 0.05 * (input.radius / 10.0);
        
        float4 bgSharp = txBase.SampleLevel(samLinear, refracTex, 0);
        float4 bgBlur = txBase.SampleLevel(samLinear, refracTex, 3.0);
        float maskRefrac = txMask.SampleLevel(samLinear, refracTex, 0).r;
        float fogVal = fbm(refracTex * 8.0);
        float4 fogCol = float4(0.9, 0.9, 0.95, 1.0) * (0.85 + 0.15 * fogVal);
        finalColor = lerp(lerp(bgBlur, fogCol, 0.7), bgSharp, smoothstep(0.0, 1.0, maskRefrac));
        
        float3 lightDir = normalize(float3(-1.0, -1.0, 1.5));
        float spec = pow(max(0.0, dot(normal, lightDir)), 64.0);
        finalColor += float4(spec.rrr, 0.0);
        
        float shadow = lerp(0.7, 1.0, max(0.0, dot(normal, float3(0,1,0))));
        finalColor *= shadow;
    } else {
        float2 refracTex = texcoord + float2(0.0, -0.02) * trailAlpha;
        float4 bgSharp = txBase.SampleLevel(samLinear, refracTex, 0);
        float4 bgBlur = txBase.SampleLevel(samLinear, refracTex, 3.0);
        float maskRefrac = txMask.SampleLevel(samLinear, refracTex, 0).r;
        float fogVal = fbm(refracTex * 8.0);
        float4 fogCol = float4(0.9, 0.9, 0.95, 1.0) * (0.85 + 0.15 * fogVal);
        finalColor = lerp(lerp(bgBlur, fogCol, 0.7), bgSharp, smoothstep(0.0, 1.0, maskRefrac));
        finalColor.rgb *= lerp(1.0, 1.15, trailAlpha);
    }
    
    return float4(finalColor.rgb, shapeAlpha * input.alpha);
}
