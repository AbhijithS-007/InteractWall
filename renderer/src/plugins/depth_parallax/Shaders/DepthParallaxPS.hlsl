Texture2D BaseTexture : register(t0);
Texture2D DepthTexture : register(t1);
SamplerState Sampler : register(s0);

cbuffer ParallaxCB : register(b0) {
    float2 cursorUV;
    float parallaxStrength;
    float padding;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET {
    // Read depth value (0.0 to 1.0)
    float depthValue = DepthTexture.Sample(Sampler, input.uv).r;
    
    // Depth Anything outputs 1.0 (White) for Near and 0.0 (Black) for Far.
    // Calculate cursor offset from center of screen (0.5, 0.5).
    float2 cursorOffset = float2(0.5f, 0.5f) - cursorUV;
    
    // Calculate the pixel shift. Near objects move more than far objects.
    float2 shift = parallaxStrength * depthValue * cursorOffset;
    
    // Sanity Clamp: Cap the maximum possible pixel offset magnitude to a fixed safe value (e.g. 1.5% of screen).
    float shiftMag = length(shift);
    if (shiftMag > 0.015f) {
        shift = (shift / shiftMag) * 0.015f;
    }
    
    // Explicit UV clamp: Ensure sample coordinates stay within valid [0,1] range
    // with a tiny margin to avoid hitting the exact border.
    float2 finalUV = clamp(input.uv + shift, float2(0.001f, 0.001f), float2(0.999f, 0.999f));
    
    return BaseTexture.Sample(Sampler, finalUV);
}
