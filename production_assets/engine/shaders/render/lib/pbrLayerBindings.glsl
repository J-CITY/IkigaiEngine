layout(std140, set=0, binding=6) uniform PbrLayerParameters {
    vec4 Alpha;
    vec4 Base;
#if IKIGAI_PBR_LAYER_COUNT > 0
    vec4 Layer0Color;
    vec4 Layer0Factors;
    vec4 Layer0UV;
    vec4 Layer0MaskUV;
    vec4 Layer0Blend;
    vec4 Layer0Height;
#endif
#if IKIGAI_PBR_LAYER_COUNT > 1
    vec4 Layer1Color;
    vec4 Layer1Factors;
    vec4 Layer1UV;
    vec4 Layer1MaskUV;
    vec4 Layer1Blend;
    vec4 Layer1Height;
#endif
#if IKIGAI_PBR_LAYER_COUNT > 2
    vec4 Layer2Color;
    vec4 Layer2Factors;
    vec4 Layer2UV;
    vec4 Layer2MaskUV;
    vec4 Layer2Blend;
    vec4 Layer2Height;
#endif
} pbr_Layers;
#if IKIGAI_PBR_LAYER_COUNT > 0
layout(set=0, binding=16) uniform sampler2D pbr_Layer0AlbedoMap;
layout(set=0, binding=17) uniform sampler2D pbr_Layer0NormalMap;
layout(set=0, binding=18) uniform sampler2D pbr_Layer0OrmMap;
layout(set=0, binding=19) uniform sampler2D pbr_Layer0HeightMap;
layout(set=0, binding=20) uniform sampler2D pbr_Layer0MaskMap;
#endif
#if IKIGAI_PBR_LAYER_COUNT > 1
layout(set=0, binding=21) uniform sampler2D pbr_Layer1AlbedoMap;
layout(set=0, binding=22) uniform sampler2D pbr_Layer1NormalMap;
layout(set=0, binding=23) uniform sampler2D pbr_Layer1OrmMap;
layout(set=0, binding=24) uniform sampler2D pbr_Layer1HeightMap;
layout(set=0, binding=25) uniform sampler2D pbr_Layer1MaskMap;
#endif
#if IKIGAI_PBR_LAYER_COUNT > 2
layout(set=0, binding=26) uniform sampler2D pbr_Layer2AlbedoMap;
layout(set=0, binding=27) uniform sampler2D pbr_Layer2NormalMap;
layout(set=0, binding=28) uniform sampler2D pbr_Layer2OrmMap;
layout(set=0, binding=29) uniform sampler2D pbr_Layer2HeightMap;
layout(set=0, binding=30) uniform sampler2D pbr_Layer2MaskMap;
#endif
