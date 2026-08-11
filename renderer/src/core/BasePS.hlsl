Texture2D texBase : register(t0);
SamplerState smp : register(s0);

struct VS_OUTPUT {
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

float4 main(VS_OUTPUT input) : SV_Target {
    return texBase.Sample(smp, input.Tex);
}
