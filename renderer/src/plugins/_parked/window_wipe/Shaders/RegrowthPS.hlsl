Texture2D txMask : register(t0);
SamplerState samLinear : register(s0);

cbuffer RegrowthCB : register(b0) {
    float regrowthSpeed;
    float time;
    float2 padding;
}

float hash(float2 p) { return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453); }
float noise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    f = f*f*(3.0-2.0*f);
    return lerp(lerp(hash(i), hash(i + float2(1,0)), f.x),
                lerp(hash(i + float2(0,1)), hash(i + float2(1,1)), f.x), f.y);
}

float4 main(float4 pos : SV_POSITION, float2 texcoord : TEXCOORD) : SV_TARGET {
    float currentMask = txMask.Sample(samLinear, texcoord).r;
    float n = noise(texcoord * 20.0 + time * 0.1);
    float decay = regrowthSpeed * (0.5 + 0.5 * n);
    return float4(saturate(currentMask - decay).rrr, 1.0);
}
