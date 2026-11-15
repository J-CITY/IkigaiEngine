#version 450
layout(location=0) in vec2 UV;
layout(location=1) in vec4 Tint;
layout(location=2) in vec3 WorldPosition;
layout(location=0) out vec4 Color;
layout(location=1) out vec4 ReactiveMask;
layout(set=0,binding=8) uniform sampler2D Image;
layout(set=0,binding=9) uniform sampler2D SceneDepth;
layout(std140,set=0,binding=0) uniform EngineUBO {
    mat4 View; mat4 Projection; vec3 ViewPos; float Time;
    vec2 ViewportSize; float FPS; int FrameCount;
} engine_UBO;
layout(std140,set=0,binding=3) uniform FrameData {
    mat4 InvViewProjection; mat4 PreviousViewProjection; mat4 PreviousModel;
    vec4 CameraParams; vec4 FrameState;
} engine_Frame;
void main() {
    int kind=int(floor(UV.x/2));
    vec2 p=vec2(UV.x-float(kind)*2,UV.y)*2-1;
    float alpha;
    if(kind==0) alpha=(1-smoothstep(.1,1,abs(p.x)))*(1-smoothstep(.4,1,abs(p.y)));
    else if(kind==1) alpha=1-smoothstep(.15,1,length(p));
    else alpha=(1-smoothstep(.1,.3,abs(length(p)-.65)));
    alpha*=texture(Image,p*.5+.5).a;
    vec2 screenUV=gl_FragCoord.xy/engine_UBO.ViewportSize;
    float sceneDepth=texture(SceneDepth,screenUV).r;
    if(sceneDepth<.999999) {
        vec2 clipUV=vec2(screenUV.x,mix(screenUV.y,1-screenUV.y,engine_Frame.CameraParams.w));
        float clipDepth=mix(sceneDepth*2-1,sceneDepth,engine_Frame.CameraParams.z);
        vec4 sceneH=engine_Frame.InvViewProjection*vec4(clipUV*2-1,clipDepth,1);
        vec3 sceneWorld=sceneH.xyz/max(abs(sceneH.w),1e-6)*sign(sceneH.w);
        float separation=-(engine_UBO.View*vec4(sceneWorld-WorldPosition,0)).z;
        float fadeDistance=kind==0?.12:.3;
        alpha*=smoothstep(0,fadeDistance,separation);
    }
    if(alpha<.01) discard;
    Color=vec4(Tint.rgb,Tint.a*alpha);
    ReactiveMask=vec4(1,1,1,Tint.a*alpha);
}
