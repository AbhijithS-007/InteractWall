cbuffer DropletCB : register(b0) {
    float screenWidth;
    float screenHeight;
    int qualityTier;
    float padding;
}

struct VS_INPUT {
    uint vertexId : SV_VertexID;
    uint instanceId : SV_InstanceID;
    float2 instPos : TEXCOORD0; 
    float instRadius : TEXCOORD1; 
    float instAlpha : COLOR0;
    float instTrail : COLOR1; 
    float instSeed : COLOR2;
};

struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float2 screenPos : TEXCOORD0;
    float2 center : TEXCOORD1;
    float radius : COLOR0;
    float alpha : COLOR1;
    float trail : COLOR2;
    float seed : COLOR3;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    
    float2 centerPx = input.instPos * float2(screenWidth, screenHeight);
    float trail = input.instTrail;
    float radius = input.instRadius;
    
    float2 quadUV = float2((input.vertexId << 1) & 2, input.vertexId & 2);
    
    float quadX = (quadUV.x * 2.0 - 1.0);
    float maxMeander = (qualityTier > 0) ? 0.05 * screenWidth : 0.0;
    float expandedRadius = radius + maxMeander;
    
    float x = centerPx.x + quadX * expandedRadius;
    float y = centerPx.y + (quadUV.y * 2.0 - 1.0) * radius;
    
    if (quadUV.y < 0.5) y -= trail;
    
    output.pos = float4(x / screenWidth * 2.0 - 1.0, -(y / screenHeight * 2.0 - 1.0), 0.0, 1.0);
    output.screenPos = float2(x, y);
    output.center = centerPx;
    output.radius = radius;
    output.alpha = input.instAlpha;
    output.trail = trail;
    output.seed = input.instSeed;
    
    return output;
}
