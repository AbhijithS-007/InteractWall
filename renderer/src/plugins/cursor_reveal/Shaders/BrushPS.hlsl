cbuffer BrushSettings : register(b0) {
    float2 mousePos;
    float brushSize;
    float brushHardness;
};

struct VS_OUTPUT {
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

float4 main(VS_OUTPUT input) : SV_Target {
    // Pos is in pixel coordinates of the render target
    float2 pixelPos = input.Pos.xy;
    float dist = distance(pixelPos, mousePos);
    
    // Smoothstep for soft brush edge
    // hardness: 0.0 -> linear falloff from center to edge
    // hardness: 1.0 -> hard edge at brushSize
    float innerRadius = brushSize * brushHardness;
    float mask = 1.0f - smoothstep(innerRadius, brushSize, dist);
    
    return float4(mask, mask, mask, mask);
}
