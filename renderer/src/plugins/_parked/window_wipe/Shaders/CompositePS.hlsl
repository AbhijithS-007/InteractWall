Texture2D txBase : register(t0);
Texture2D txMask : register(t1);
SamplerState samLinear : register(s0);

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

float4 main(float4 pos : SV_POSITION, float2 texcoord : TEXCOORD) : SV_TARGET {
    float4 baseColor = txBase.SampleLevel(samLinear, texcoord, 3.0); // Blurred base
    float4 sharpBase = txBase.Sample(samLinear, texcoord);
    float mask = txMask.Sample(samLinear, texcoord).r;
    
    float fogVal = fbm(texcoord * 8.0);
    float4 fogColor = float4(0.9, 0.9, 0.95, 1.0) * (0.85 + 0.15 * fogVal);
    
    float4 foggedBase = lerp(baseColor, fogColor, 0.7);
    
    return lerp(foggedBase, sharpBase, smoothstep(0.0, 1.0, mask));
}
