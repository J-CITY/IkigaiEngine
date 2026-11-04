struct _8
{
    float2 TexCoord;
};

cbuffer _ubo : register(b1)
{
    row_major float4x4 ubo_projection : packoffset(c0);
    row_major float4x4 ubo_view : packoffset(c4);
    row_major float4x4 ubo_model : packoffset(c8);
};


static float4 gl_Position;
static _8 Out;
static float2 aTexCoord;
static float3 aPosition;

struct SPIRV_Cross_Input
{
    float3 aPosition : TEXCOORD0;
    float2 aTexCoord : TEXCOORD1;
};

struct SPIRV_Cross_Output
{
    _8 Out : TEXCOORD0;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    Out.TexCoord = aTexCoord;
    gl_Position = mul(float4(aPosition, 1.0f), mul(ubo_model, mul(ubo_view, ubo_projection)));
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    aTexCoord = stage_input.aTexCoord;
    aPosition = stage_input.aPosition;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.Out = Out;
    return stage_output;
}
