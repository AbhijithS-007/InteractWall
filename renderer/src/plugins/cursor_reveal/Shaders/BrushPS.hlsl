cbuffer BrushSettings : register(b0) {
    float4 packedPoints[8];
    int numPoints;
    float brushSize;
    float brushHardness;
    float padding;
};

struct VS_OUTPUT {
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

float distanceToSegment(float2 p, float2 a, float2 b) {
    float2 pa = p - a;
    float2 ba = b - a;
    float ba2 = dot(ba, ba);
    if (ba2 < 0.0001f) return length(pa);
    float h = saturate(dot(pa, ba) / ba2);
    return length(pa - ba * h);
}

float2 getPoint(uint index) {
    uint vecIdx = index >> 1;
    uint subIdx = index & 1;
    return (subIdx == 0) ? packedPoints[vecIdx].xy : packedPoints[vecIdx].zw;
}

float4 main(VS_OUTPUT input) : SV_Target {
    float2 pixelPos = input.Pos.xy;
    
    float minDist = 999999.0f;
    for (uint i = 0; i < (uint)(max(0, numPoints - 1)); ++i) {
        float2 p1 = getPoint(i);
        float2 p2 = getPoint(i + 1);
        float d = distanceToSegment(pixelPos, p1, p2);
        minDist = min(minDist, d);
    }
    
    float innerRadius = brushSize * brushHardness;
    float mask = 1.0f - smoothstep(innerRadius, brushSize, minDist);
    
    return float4(mask, mask, mask, mask);
}
