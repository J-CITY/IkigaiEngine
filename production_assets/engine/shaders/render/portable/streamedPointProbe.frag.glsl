#version 450
layout(std140,set=0,binding=0) uniform EngineUBO {
mat4 View; mat4 Projection; vec3 ViewPos; float Time;
vec2 ViewportSize; float FPS; int FrameCount;
} engine_UBO;
layout(std140,set=0,binding=3) uniform FrameData {
mat4 InvViewProjection; mat4 PreviousViewProjection; mat4 PreviousModel;
vec4 CameraParams; vec4 FrameState;
} engine_Frame;
layout(std140,set=0,binding=5) uniform CurrentShadow {
mat4 Matrices[4]; vec4 CascadeSplits; vec4 PositionRange; vec4 Settings; vec4 LightInfo;
} engine_CurrentShadow;
layout(location=0) in vec2 UV;
layout(location=0) out vec4 Color;
layout(set=0,binding=8) uniform samplerCube PointShadow;
layout(set=0,binding=9) uniform sampler2D DirectionalShadow;
layout(set=0,binding=10) uniform sampler2D SpotShadow;
layout(std140,set=0,binding=4) uniform Parameters { int Face; float Distance; } data_UBO;

bool inside(vec2 uv) { return all(greaterThanEqual(uv,vec2(0))) && all(lessThanEqual(uv,vec2(1))); }
vec3 worldPosition(vec2 uv,float depth) {
uv.y=mix(uv.y,1-uv.y,engine_Frame.CameraParams.w);
vec4 h=engine_Frame.InvViewProjection*vec4(uv*2.0-1.0,mix(depth*2.0-1.0,depth,engine_Frame.CameraParams.z),1);
return h.xyz/max(abs(h.w),1e-6)*sign(h.w);
}
vec3 projectWorld(vec3 p) {
vec4 h=engine_UBO.Projection*engine_UBO.View*vec4(p,1);
vec2 uv=h.xy/max(h.w,1e-6)*0.5+0.5;
uv.y=mix(uv.y,1-uv.y,engine_Frame.CameraParams.w); return vec3(uv,h.w);
}
float viewDepth(vec3 p) { return -(engine_UBO.View*vec4(p,1)).z; }

vec3 faceDirection(vec2 uv,int face) {
vec2 p=uv*2-1;
if(face==0) return normalize(vec3(1,-p.y,-p.x));
if(face==1) return normalize(vec3(-1,-p.y,p.x));
if(face==2) return normalize(vec3(p.x,1,p.y));
if(face==3) return normalize(vec3(p.x,-1,-p.y));
if(face==4) return normalize(vec3(p.x,-p.y,1));
return normalize(vec3(-p.x,-p.y,-1));
}
// Fullscreen UV already addresses the destination texture on every backend.
// Cubemap face coordinates use that address directly, including the -Y rule.
vec2 canonicalUV(vec2 uv) { return uv; }
layout(std140,set=0,binding=7) uniform PointShadowParameters {
float Bias; float NormalBias; float Radius; int Samples;
} point_UBO;

float pointVisibility(vec3 p) {
vec3 delta=p-engine_CurrentShadow.PositionRange.xyz;
float d=length(delta),farPlane=engine_CurrentShadow.PositionRange.w;
if(d<max(engine_CurrentShadow.Settings.y,1e-5) || d>=farPlane || farPlane<=0) return 1;
vec3 ray=delta/d;
vec3 tangent=normalize(cross(ray,abs(ray.y)<.95?vec3(0,1,0):vec3(1,0,0)));
vec3 bitangent=cross(ray,tangent);float visibility=0;
int taps=clamp(point_UBO.Samples,1,32);
for(int i=0;i<taps;++i) {
float r=sqrt((float(i)+.5)/float(taps))*max(point_UBO.Radius,0)/d;
float angle=float(i)*2.39996323;
vec3 sampleRay=ray+r*(cos(angle)*tangent+sin(angle)*bitangent);
float stored=textureLod(PointShadow,sampleRay,0).r*farPlane;
visibility+=step(d-max(point_UBO.Bias,0),stored);
} return visibility/float(taps);
}

float shadowPCF(int matrixIndex,vec3 p,sampler2D atlas,vec2 tile,vec2 grid) {
vec4 h=engine_CurrentShadow.Matrices[matrixIndex]*vec4(p,1);
if(h.w<=0) return 1;
vec3 ndc=h.xyz/h.w; vec2 localUV=ndc.xy*0.5+0.5;
localUV.y=mix(localUV.y,1-localUV.y,engine_Frame.CameraParams.w);
float depth=mix(ndc.z*0.5+0.5,ndc.z,engine_Frame.CameraParams.z);
if(!inside(localUV)||depth<0||depth>1) return 1;
vec2 texel=1.0/vec2(textureSize(atlas,0)); vec2 uv=(localUV+tile)/grid;
vec2 low=tile/grid+texel*0.5,high=(tile+1)/grid-texel*0.5;
float visibility=0;
for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) visibility+=step(depth-engine_CurrentShadow.Settings.x,texture(atlas,clamp(uv+vec2(x,y)*texel,low,high)).r);
return visibility/9;
}

float shadowFor(int index,vec3 p) {
if(engine_CurrentShadow.LightInfo.z<.5) return 1;
int type=int(engine_CurrentShadow.LightInfo.y);
if(type==1) {
float depth=viewDepth(p);int c=0;
if(depth>engine_CurrentShadow.CascadeSplits.x)c=1;
if(depth>engine_CurrentShadow.CascadeSplits.y)c=2;
if(depth>engine_CurrentShadow.CascadeSplits.z)c=3;
return shadowPCF(c,p,DirectionalShadow,vec2(c%2,c/2),vec2(2));
}
if(type==2) return shadowPCF(0,p,SpotShadow,vec2(0),vec2(1));
return pointVisibility(p);
}
float shadowSurface(int index,vec3 p,vec3 n) {
return shadowFor(index,p+(int(engine_CurrentShadow.LightInfo.y)==0?n*max(point_UBO.NormalBias,0):vec3(0)));
}

void main() {
vec3 p=engine_CurrentShadow.PositionRange.xyz+faceDirection(UV,data_UBO.Face)*data_UBO.Distance;
float visibility=shadowFor(int(engine_CurrentShadow.LightInfo.x),p);Color=vec4(vec3(visibility),1);
}
