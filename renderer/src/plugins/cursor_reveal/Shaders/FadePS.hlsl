cbuffer FadeSettings : register(b0) {
    float fadeAmount;
    float3 padding;
};

struct VS_OUTPUT {
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

float4 main(VS_OUTPUT input) : SV_Target {
    // Outputs the fade amount to be subtracted from the mask using a Subtractive Blend State
    return float4(fadeAmount, fadeAmount, fadeAmount, fadeAmount);
}
