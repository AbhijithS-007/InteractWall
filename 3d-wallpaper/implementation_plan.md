# Goal Description
Fix the pixel shader bug causing all tiers to render near-black, and implement the intended IBL (Image-Based Lighting) sampling logic for the Balanced and High quality tiers while keeping the Low tier strictly on the clamped-diffuse path. We will also perform rigorous verification to ensure no resource leaks remain and that quality tiers execute correctly.

## User Review Required
No major architectural changes, but the pixel shader will be updated to correctly sample IBL textures.

## Open Questions
None.

## Proposed Changes

### `shaders/ModelPS.hlsl`
- **[MODIFY] ModelPS.hlsl**: 
  - Revert the debug return statement to `return float4(finalColor, albedo.a);`. 
  - Add logic to check `iblTier`. 
    - If `iblTier == 0` (Low), use the existing `fakeIrradiance` and `envColor` (clamped diffuse/specular).
    - If `iblTier > 0` (Balanced/High), sample `irradianceMap` and `prefilteredMap` for ambient lighting.
    - Use `prefilteredMipCount` to sample the correct mip level from `prefilteredMap` based on material roughness.

### `src/main.cpp`
- **[MODIFY] main.cpp**:
  - Re-enable the `g_ibl.Initialize` call.
  - Print the `iblTier` in the debug log to confirm it accurately matches the UI settings.

### `src/TextureLoader.cpp`
- **[MODIFY] TextureLoader.cpp**:
  - Implement the WIC scaling to restrict textures to max 1024x1024 for `Balanced` tier and 2048x2048 for `High` tier.

## Verification Plan

### Automated Tests
- Build the C++ project.

### Manual Verification
1. **Texture Capping Fix for Memory**: 
   - I will run the D3D11 debug layer `ReportLiveDeviceObjects(D3D11_RLDO_DETAIL)` after a complete bake-and-render cycle.
   - I will provide the live-object dump as evidence to confirm no intermediate IBL bake resources (mip buffers/RTVs) are being leaked.
   - I will report the final RAM figure compared to the baseline (~150MB).
2. **Re-verify the Visual Corruption Bug**: 
   - I will explicitly confirm if the duplicated/mirrored geometry and interior visibility bug is resolved. (It is highly likely that forcing `albedo.a = 1.0` in the debug line caused glass to render completely opaque without depth writes, leading to the depth-sorting artifacts observed).
3. **Strengthen Verification**: 
   - I will report the GPU execution cost per tier via the timestamp queries (confirming High > Balanced > Low).
   - I will confirm the developer mode debug line shows `iblTier` properly syncing with the UI selection.
   - I will verify the visual difference between the tiers (Low matching the original pre-IBL look, Balanced/High showing proper gloss and reflections).
