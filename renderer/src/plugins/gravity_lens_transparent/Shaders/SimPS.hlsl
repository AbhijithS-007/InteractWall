Texture2D PrevStateTex : register(t0);
SamplerState Sampler : register(s0);

cbuffer SimCB : register(b0) {
    float4 packedPoints[16];
    float pressRadius;
    float pressDepth;
    float stiffness;
    float damping;
    float deltaTime;
    float aspectRatio;
    int numPoints;
    float fadeDecay;   // Per-frame multiplier for displacement decay (0=instant, 1=no decay)
}

struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float4 main(VS_OUTPUT input) : SV_TARGET {
    // Current state (displacement XY, velocity ZW)
    float4 state = PrevStateTex.SampleLevel(Sampler, input.uv, 0);
    float2 disp = state.xy;
    float2 vel = state.zw;
    
    float2 totalTargetDisp = float2(0, 0);
    float totalInfluence = 0.0;
    
    // Hard clamp to prevent kaleidoscope artifacts on corrupted config files
    float safeRadius = clamp(pressRadius, 0.01, 0.5);
    float safeDepth = clamp(pressDepth, 0.0, 0.15);
    
    for (int i = 0; i < numPoints; i++) {
        uint vecIdx = i >> 1;
        uint subIdx = i & 1;
        float2 pt = (subIdx == 0) ? packedPoints[vecIdx].xy : packedPoints[vecIdx].zw;
        
        float2 toPt = pt - input.uv;
        float2 aspectToPt = toPt;
        aspectToPt.x *= aspectRatio;
        float dist = length(aspectToPt);
        
        float pull = 1.0 - smoothstep(0.0, safeRadius, dist);
        if (pull > 0.001) {
            float rawDist = length(toPt);
            float2 dir = (rawDist > 0.001) ? (toPt / rawDist) : float2(0,0);
            
            // Older points have less pull (trail fade)
            float ageFade = 1.0 - ((float)i / (float)max(1, numPoints)); 
            
            totalTargetDisp += dir * pull * safeDepth * ageFade;
            totalInfluence += pull * ageFade;
        }
    }
    
    // Damped spring physics
    float2 force = (totalTargetDisp - disp) * stiffness;
    vel += force * deltaTime;
    vel *= damping;
    disp += vel * deltaTime;

    // When cursor has no influence on this pixel, decay displacement toward zero
    // fadeDecay=1.0 means no decay; fadeDecay=0.0 means instant reset
    if (totalInfluence < 0.001) {
        // Compute a per-frame decay factor from the 60fps-normalized user value
        float decayPerFrame = pow(abs(fadeDecay), deltaTime * 60.0);
        disp *= decayPerFrame;
        vel *= decayPerFrame;
    }
    
    return float4(disp.x, disp.y, vel.x, vel.y);
}
