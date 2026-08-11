Texture2D txMask : register(t0);
SamplerState samLinear : register(s0);

cbuffer BrushCB : register(b0) {
    float4 packedPoints[8];
    int numPoints;
    float brushSize;
    float wipeRoughness;
    float time;
}

float hash(float2 p) { return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453); }
float noise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    f = f*f*(3.0-2.0*f);
    return lerp(lerp(hash(i), hash(i + float2(1,0)), f.x),
                lerp(hash(i + float2(0,1)), hash(i + float2(1,1)), f.x), f.y);
}

float sdCapsule(float2 p, float2 a, float2 b) {
    float2 pa = p - a, ba = b - a;
    float h = saturate(dot(pa, ba) / max(dot(ba, ba), 1e-5));
    return length(pa - ba * h);
}

float4 main(float4 pos : SV_POSITION, float2 texcoord : TEXCOORD) : SV_TARGET {
    float currentMask = txMask.Sample(samLinear, texcoord).r;
    if (numPoints < 2) return float4(currentMask.rrr, 1.0);
    
    float minDist = 9999.0;
    for (int i = 0; i < numPoints - 1; i++) {
        float2 p1 = float2(packedPoints[i/2][(i%2)*2], packedPoints[i/2][(i%2)*2+1]);
        float2 p2 = float2(packedPoints[(i+1)/2][((i+1)%2)*2], packedPoints[(i+1)/2][((i+1)%2)*2+1]);
        minDist = min(minDist, sdCapsule(pos.xy, p1, p2));
    }
    
    float n = noise(pos.xy * 0.02 + time * 0.5) * wipeRoughness * 5.0;
    float dist = minDist + n;
    
    float softEdge = max(2.0, brushSize * 0.2);
    float alpha = 1.0 - smoothstep(brushSize - softEdge, brushSize, dist);
    return float4(max(currentMask, alpha).rrr, 1.0);
}
