import os

filepath = r'C:\My_Proj\InteractWall\renderer\src\plugins\stone_press_v2\Shaders\SP2_CompositePS.hlsl'
with open(filepath, 'r', encoding='utf-8') as f:
    code = f.read()

search_shader = """        // We scale the UI parallaxStrength (usually 0 to 0.2) by 4 to get a good range.
        // pressDepth carries the dynamic physics multiplier from the CPU!
        float dynamicStrength = parallaxStrength * pressDepth;
        float k = 1.0 - saturate(dynamicStrength * 4.0) * 0.85;
        
        // To avoid division by zero just in case
        k = max(k, 0.05);
        
        float mappedNormDist = normDist / (k + (1.0 - k) * normDist);
        
        float newDist = mappedNormDist * pressRadius;
        float pushAmt = newDist - dist;
        
        // Seamless Boundary Blending (C2 Continuity)
        // Smoothly fade out the stretch right at the edge to prevent visual creases.
        float edgeFade = smoothstep(1.0, 0.85, normDist);
        pushAmt *= edgeFade;"""

replace_shader = """        // We use a pure sine wave to guarantee a perfectly smooth curve.
        // A sine wave is the smoothest possible transition, preventing the "needle/spike" look.
        float dynamicStrength = parallaxStrength * pressDepth;
        
        // To prevent folding (lens effect), the max allowed amplitude is 1/PI (~0.318).
        // We clamp it safely below that.
        float S = clamp(dynamicStrength * 1.5, 0.0, 0.31);
        
        // Apply sine wave stretch
        float mappedNormDist = normDist + S * sin(3.14159265 * normDist);
        
        float newDist = mappedNormDist * pressRadius;
        float pushAmt = newDist - dist;
        
        // The sine wave naturally goes to 0 at normDist=1, but we can ensure seamless edge:
        pushAmt *= smoothstep(1.0, 0.9, normDist);"""
        
code = code.replace(search_shader, replace_shader)

with open(filepath, 'w', encoding='utf-8') as f:
    f.write(code)

