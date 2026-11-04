#version 450
// Cosine-lobe integration per probe. Atlas cells are equal-weight strata of the cosine lobe about the probe normal,
// so the mean cell radiance is the Monte Carlo estimate of irradiance / pi (the unit of the Indirect contract).
// Output: rgb = Intensity * E / pi, a = confidence = mean sample confidence by source (screen hit 1, ray query hit
// RayQueryConfidence, DDGI / environment MissConfidence; 0 for invalid probes).
#include "screenProbeCommon.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=8) uniform sampler2D Radiance;
layout(set=0,binding=9,rgba16f) uniform writeonly image2D Irradiance;
void main() {
    ivec2 probe=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(probe,imageSize(Irradiance))))return;
    int side=spDirectionSide();
    ivec2 base=probe*side;
    if(any(greaterThan(base+side,textureSize(Radiance,0)))) {
        imageStore(Irradiance,probe,vec4(0.0));
        return;
    }
    vec3 sum=vec3(0.0);
    float valid=0.0;
    float confidence=0.0;
    for(int y=0;y<side;++y) {
        for(int x=0;x<side;++x) {
            vec4 sampleValue=texelFetch(Radiance,base+ivec2(x,y),0);
            sum+=sampleValue.rgb;
            valid+=spSampleSource(sampleValue.a)!=SP_SOURCE_INVALID?1.0:0.0;
            confidence+=spSampleConfidence(sampleValue.a);
        }
    }
    float count=float(side*side);
    if(valid<count) {
        imageStore(Irradiance,probe,vec4(0.0));
        return;
    }
    imageStore(Irradiance,probe,vec4(sum/count*max(ScreenProbe.Trace.y,0.0),confidence/count));
}
