Texture2D StateTex : register(t0);
SamplerState Sampler : register(s0);

cbuffer SimCB : register(b0) {
    float2 cursorUV;
    float pressRadius;
    float pressDepth;
    float stiffness;
    float damping;
    float deltaTime;
    float aspectRatio;
}

struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

// Returns float2(Height, Velocity)
float2 main(VS_OUTPUT input) : SV_TARGET {
    // Read current state: x = height, y = velocity
    float2 state = StateTex.SampleLevel(Sampler, input.uv, 0).xy;
    float currentHeight = state.x;
    float currentVelocity = state.y;
    
    // Calculate distance to cursor (adjusting for aspect ratio for perfect circle)
    float2 distVec = input.uv - cursorUV;
    distVec.x *= aspectRatio;
    float distToCursor = length(distVec);
    
    // Calculate target height
    // Inside pressRadius, height smoothsteps down to -pressDepth at the exact center (dist=0)
    // Outside pressRadius, height is 0 (flat)
    float targetHeight = -pressDepth * smoothstep(pressRadius, 0.0, distToCursor);
    
    // Spring physics integration
    float force = (targetHeight - currentHeight) * stiffness;
    currentVelocity += force * deltaTime;
    currentVelocity *= damping;
    currentHeight += currentVelocity * deltaTime;
    
    return float2(currentHeight, currentVelocity);
}
