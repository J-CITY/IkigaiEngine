layout(std140,set=0,binding=0) uniform EngineUBO {
    mat4 View;mat4 Projection;vec3 ViewPos;float Time;
    vec2 ViewportSize;float FPS;int FrameCount;
} engine_UBO;
layout(std140,set=0,binding=1) uniform FrameData {
    mat4 InvViewProjection;mat4 PreviousViewProjection;mat4 PreviousModel;
    vec4 CameraParams;vec4 FrameState;
} engine_Frame;
layout(std140,set=0,binding=2) uniform RayFrame {vec4 State;} engine_RayFrame;
vec3 worldPosition(vec2 uv,float depth) {
    uv.y=mix(uv.y,1-uv.y,engine_Frame.CameraParams.w);
    vec4 p=engine_Frame.InvViewProjection*vec4(uv*2-1,mix(depth*2-1,depth,engine_Frame.CameraParams.z),1);
    return p.xyz/p.w;
}
uint rngState;
float randomFloat() {rngState=rngState*747796405u+2891336453u;uint w=((rngState>>((rngState>>28u)+4u))^rngState)*277803737u;return float((w>>22u)^w)/4294967296.0;}
vec3 cosineDirection(vec3 n) {
    float a=6.2831853*randomFloat(),r=sqrt(randomFloat());
    vec3 t=normalize(cross(abs(n.y)<.99?vec3(0,1,0):vec3(1,0,0),n));
    return normalize(t*(r*cos(a))+cross(n,t)*(r*sin(a))+n*sqrt(max(0,1-r*r)));
}
vec3 environment(vec3 d) {return mix(vec3(.08,.1,.14),vec3(.6,.75,1),clamp(d.y*.5+.5,0,1));}
