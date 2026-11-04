struct _16
{
    float2 TexCoord;
};

Texture2D<float4> sTexture : register(t0);
SamplerState _sTexture_sampler : register(s0);

struct Light {
    float3 pos;
    float cutoff;
    float3 forward;
    float outerCutoff;
    float3 color;
    float constant;
    int type;
    float linear1;
    float quadratic;
    float intensity;
    float radius;
    float3 padding;
};

StructuredBuffer<Light> sLights : register(t1);

static float4 result;
static _16 In;

struct SPIRV_Cross_Input
{
    _16 In : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 result : SV_Target0;
};

void frag_main()
{
    result = sTexture.Sample(_sTexture_sampler, In.TexCoord);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    In = stage_input.In;
    frag_main();
    SPIRV_Cross_Output stage_output;

    result *= float4(sLights[0].color.r, sLights[0].color.g, sLights[0].color.b, 1.0);

    stage_output.result = result;
    return stage_output;
}
