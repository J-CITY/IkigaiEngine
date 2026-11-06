const float GlossyMaxTracedRoughness=0.8;
const float GlossyFadeStartRoughness=0.65;

float glossyTraceWeight(float roughness) {
    return 1-smoothstep(GlossyFadeStartRoughness,GlossyMaxTracedRoughness,roughness);
}
