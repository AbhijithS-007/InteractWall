// ModelVS.hlsl — Vertex shader for 3D models with lighting
// Transforms vertices to clip space, and passes world-space normals to PS.

cbuffer CBTransform : register(b0) {
    float4x4 worldViewProj;
    float4x4 world;
    float4x4 worldInverseTranspose;
};

struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 tangent  : TANGENT;
    float2 texcoord1: TEXCOORD1;
};

struct VSOutput {
    float4 position     : SV_POSITION;
    float2 texcoord     : TEXCOORD0;
    float3 normal       : NORMAL;
    float3 worldPos     : TEXCOORD2;
    float3x3 tbn        : TANGENT_MATRIX;
    float2 texcoord1    : TEXCOORD1;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    // Position to clip space
    output.position = mul(float4(input.position, 1.0), worldViewProj);
    
    // Position to world space (for view direction in specular)
    output.worldPos = mul(float4(input.position, 1.0), world).xyz;
    
    output.texcoord1 = input.texcoord1;
    
    // Normal to world space (using inverse transpose)
    output.normal = mul(input.normal, (float3x3)worldInverseTranspose);
    output.normal = normalize(output.normal);
    
    // Tangent to world space
    float3 T = mul(input.tangent.xyz, (float3x3)worldInverseTranspose);
    T = normalize(T);
    
    // Re-orthogonalize T with respect to N (Gram-Schmidt process)
    T = normalize(T - dot(T, output.normal) * output.normal);
    
    // Compute Bitangent
    float3 B = cross(output.normal, T) * input.tangent.w;
    
    // Output TBN matrix
    output.tbn = float3x3(T, B, output.normal);
    
    output.texcoord = input.texcoord;
    
    return output;
}
