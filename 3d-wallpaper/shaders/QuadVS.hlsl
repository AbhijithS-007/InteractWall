struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

PSInput VSMain(uint vertexID : SV_VertexID) {
    PSInput output;

    // CCW winding:
    // v0: (0) -> (-1,  1) [tex 0, 0]
    // v1: (1) -> (-1, -3) [tex 0, 2]
    // v2: (2) -> ( 3,  1) [tex 2, 0]
    output.texcoord = float2(vertexID == 2 ? 2.0 : 0.0, vertexID == 1 ? 2.0 : 0.0);
    output.position = float4(output.texcoord * float2(2, -2) + float2(-1, 1), 0.0, 1.0);

    return output;
}
