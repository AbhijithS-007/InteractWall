Texture2D texA : register(t0);
Texture2D texB : register(t1);
Texture2D texMask : register(t2);
SamplerState smp : register(s0);

struct VS_OUTPUT {
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

float4 main(VS_OUTPUT input) : SV_Target {
    float4 colorA = texA.Sample(smp, input.Tex);
    float4 colorB = texB.Sample(smp, input.Tex);
    float mask = texMask.Sample(smp, input.Tex).r; // Use R channel of mask
    
    return lerp(colorA, colorB, mask);
}
