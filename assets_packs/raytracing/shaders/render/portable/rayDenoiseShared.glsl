layout(location=0) in vec2 UV;layout(location=0) out vec4 Color;
layout(set=0,binding=8) uniform sampler2D Input;
layout(set=0,binding=9) uniform sampler2D Depth;
layout(set=0,binding=10) uniform sampler2D Normals;
layout(set=0,binding=11) uniform sampler2D Previous;
layout(set=0,binding=12) uniform sampler2D PreviousDepth;
layout(set=0,binding=13) uniform sampler2D PreviousNormals;
layout(set=0,binding=14) uniform sampler2D Velocity;
#ifdef GLOSSY_REFLECTION_DENOISE
#include "../../raytracing/glossyReflectionRoughness.glsl"
layout(set=0,binding=15) uniform sampler2D Roughness;

bool glossySameRoughness(vec2 uv,float roughness) {
    return abs(texture(Roughness,uv).r-roughness)<=0.05;
}
#endif
layout(std140,set=0,binding=3) uniform FrameData {
    mat4 InvViewProjection;mat4 PreviousViewProjection;mat4 PreviousModel;vec4 CameraParams;vec4 FrameState;
} engine_Frame;
layout(std140,set=0,binding=4) uniform Parameters {float Radius;float DepthSigma;float UseTemporal;float HistoryWeight;} data_UBO;
#ifdef RAY_DENOISE_MOTION
// Moving geometry reduces temporal weight by per-tap geometric confidence.
layout(std140,set=0,binding=5) uniform RayFrame {vec4 State;vec4 Motion;} engine_RayFrame;
#endif
void main() {
    vec2 texel=1.0/vec2(textureSize(Input,0));float d=texture(Depth,UV).r;
#ifdef GLOSSY_REFLECTION_DENOISE
    float roughness=texture(Roughness,UV).r;
    if(d>=0.999999||glossyTraceWeight(roughness)==0.0) {
        Color=texture(Input,UV);
        return;
    }
#endif
    vec3 n=normalize(texture(Normals,UV).xyz);vec4 sum=vec4(0);float total=0;
    int r=clamp(int(data_UBO.Radius),0,3);
    for(int y=-r;y<=r;++y)for(int x=-r;x<=r;++x) {
        vec2 uv=clamp(UV+vec2(x,y)*texel,texel*.5,1-texel*.5);
#ifdef GLOSSY_REFLECTION_DENOISE
        if(!glossySameRoughness(uv,roughness))continue;
#endif
        float delta=abs(texture(Depth,uv).r-d);
        float normalWeight=pow(max(dot(n,normalize(texture(Normals,uv).xyz)),0),32);
        float weight=exp(-float(x*x+y*y)*.5)*exp(-delta/max(data_UBO.DepthSigma,1e-7))*normalWeight;
        sum+=texture(Input,uv)*weight;total+=weight;
    }
    Color=total>1e-7?sum/total:texture(Input,UV);
    if(data_UBO.UseTemporal<.5||engine_Frame.FrameState.x<.5)return;
    // The RT history lives on the jittered G-buffer grid. Motion vectors
    // exclude jitter for TAA/upscalers, so restore its delta for this history.
    vec2 previousUV=UV-texture(Velocity,UV).xy-engine_Frame.FrameState.zw;
    if(any(lessThan(previousUV,vec2(0)))||any(greaterThan(previousUV,vec2(1))))return;
    if(abs(texture(PreviousDepth,previousUV).r-d)>data_UBO.DepthSigma*2)return;
    if(dot(n,normalize(texture(PreviousNormals,previousUV).xyz))<.9)return;
    vec3 low=Color.rgb,high=Color.rgb;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
        vec2 uv=UV+vec2(x,y)*texel;
#ifdef GLOSSY_REFLECTION_DENOISE
        if(!glossySameRoughness(uv,roughness))continue;
#endif
        vec3 value=texture(Input,uv).rgb;low=min(low,value);high=max(high,value);
    }
    // RT targets often have nearest samplers. Reconstruct history explicitly
    // and reject taps across silhouettes instead of switching nearest texels
    // as the subpixel jitter changes.
    ivec2 size=textureSize(Previous,0);vec2 pixel=previousUV*vec2(size)-.5;
    ivec2 base=ivec2(floor(pixel));vec2 fraction=fract(pixel);
    vec4 history=vec4(0);float historyTotal=0;
#ifdef RAY_DENOISE_MOTION
    float confidenceTotal=0;
#endif
    for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        ivec2 tap=base+ivec2(x,y);
        if(any(lessThan(tap,ivec2(0)))||any(greaterThanEqual(tap,size)))continue;
        vec2 uv=(vec2(tap)+.5)/vec2(size);
#ifdef GLOSSY_REFLECTION_DENOISE
        if(!glossySameRoughness(uv,roughness))continue;
#endif
        if(abs(texture(PreviousDepth,uv).r-d)>data_UBO.DepthSigma*2)continue;
        if(dot(n,normalize(texture(PreviousNormals,uv).xyz))<.9)continue;
        float weight=(x==0?1-fraction.x:fraction.x)*(y==0?1-fraction.y:fraction.y);
        history+=texelFetch(Previous,tap,0)*weight;historyTotal+=weight;
#ifdef RAY_DENOISE_MOTION
        float tapDepth=abs(texture(PreviousDepth,uv).r-d)/max(data_UBO.DepthSigma*2,1e-7);
        float tapNormal=clamp((dot(n,normalize(texture(PreviousNormals,uv).xyz))-.9)/.1,0,1);
        confidenceTotal+=weight*(1-clamp(tapDepth,0,1))*tapNormal;
#endif
    }
    if(historyTotal<.001)return;
    history/=historyTotal;history.rgb=clamp(history.rgb,low,high);
    float historyWeight=clamp(data_UBO.HistoryWeight,0,.95);
#ifdef RAY_DENOISE_MOTION
    historyWeight*=engine_RayFrame.Motion.x>.5?confidenceTotal/historyTotal:1.0;
#endif
    Color=mix(Color,history,historyWeight);
}
