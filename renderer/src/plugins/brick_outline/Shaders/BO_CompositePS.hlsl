Texture2D baseTex : register(t0);
SamplerState samp : register(s0);

cbuffer cbSettings : register(b0)
{
    float2 resolution;
    float2 cursorPos; // in normalized screen space [0,1] or pixels? We'll use normalized [0,1]

    float brickWidth; // in pixels
    float brickHeight; // in pixels
    float lineThickness; // in pixels
    float effectRadius; // normalized radius [0,1] based on screen height or width

    float edgeSoftness;
    float glowIntensity;
    int qualityTier; 
    float padding1;
    
    float3 outlineColor;
    float padding2;
};

struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float getBrickMask(float2 pixelPos)
{
    // Row index based on brick height
    float row = floor(pixelPos.y / brickHeight);
    
    // Offset alternating rows by half brick width
    float xOffset = fmod(row, 2.0) * (brickWidth * 0.5);
    
    float x = pixelPos.x + xOffset;
    float y = pixelPos.y;
    
    // Distance to nearest vertical edge (column boundary)
    float distX = abs(fmod(x + brickWidth * 0.5, brickWidth) - brickWidth * 0.5);
    // Distance to nearest horizontal edge (row boundary)
    float distY = abs(fmod(y + brickHeight * 0.5, brickHeight) - brickHeight * 0.5);
    
    // The closest distance to any edge
    float dist = min(distX, distY);
    
    // Smoothstep for anti-aliasing. 
    // We want the mask to be 1 at distance 0, and 0 at distance >= lineThickness / 2
    float halfThick = lineThickness * 0.5;
    return 1.0 - smoothstep(halfThick - 0.5, halfThick + 0.5, dist);
}

float4 main(VS_OUTPUT input) : SV_TARGET
{
    float4 baseColor = baseTex.Sample(samp, input.texcoord);
    
    // Current pixel position
    float2 pixelPos = input.texcoord * resolution;
    float2 cursorPixelPos = cursorPos * resolution;
    
    // Distance from cursor to pixel in absolute pixels
    float distToCursor = distance(pixelPos, cursorPixelPos);
    
    // Convert normalized effectRadius to absolute pixels based on min dimension
    float radiusPixels = effectRadius * min(resolution.x, resolution.y);
    float softnessPixels = edgeSoftness * min(resolution.x, resolution.y) + 1.0; // avoid div by 0
    
    // Radial mask
    float radialMask = 1.0 - smoothstep(radiusPixels - softnessPixels, radiusPixels, distToCursor);
    
    if (radialMask <= 0.0)
        return baseColor; // Early out outside the radius
        
    // Base mask
    float mask = getBrickMask(pixelPos);
    
    // Glow pass (multi-sample approximation)
    float glow = 0.0;
    if (qualityTier > 0)
    {
        // Balanced / High: Sample neighbors for blur
        float offset = lineThickness * 0.7; // slight offset based on line thickness
        glow += getBrickMask(pixelPos + float2(offset, 0.0));
        glow += getBrickMask(pixelPos + float2(-offset, 0.0));
        glow += getBrickMask(pixelPos + float2(0.0, offset));
        glow += getBrickMask(pixelPos + float2(0.0, -offset));
        
        if (qualityTier > 1) 
        {
            // High: Diagonals
            float dOffset = offset * 0.7071;
            glow += getBrickMask(pixelPos + float2(dOffset, dOffset));
            glow += getBrickMask(pixelPos + float2(-dOffset, dOffset));
            glow += getBrickMask(pixelPos + float2(dOffset, -dOffset));
            glow += getBrickMask(pixelPos + float2(-dOffset, -dOffset));
            glow /= 8.0;
        }
        else
        {
            glow /= 4.0;
        }
    }
    else
    {
        // Low tier: single simple offset or just thicker line approximation
        // Just use the base mask scaled slightly and heavily smoothed
        float halfThick = lineThickness * 1.5;
        float dist = min(
            abs(fmod(pixelPos.x + fmod(floor(pixelPos.y / brickHeight), 2.0) * (brickWidth * 0.5) + brickWidth * 0.5, brickWidth) - brickWidth * 0.5),
            abs(fmod(pixelPos.y + brickHeight * 0.5, brickHeight) - brickHeight * 0.5)
        );
        glow = 1.0 - smoothstep(0.0, halfThick, dist);
    }
    
    // Combine base line and glow
    float finalLine = saturate(mask + glow * glowIntensity);
    
    // Apply radial falloff
    finalLine *= radialMask;
    
    // Additive composite
    float3 lineCol = outlineColor * finalLine;
    float3 finalColor = saturate(baseColor.rgb + lineCol);
    
    return float4(finalColor, 1.0);
}
