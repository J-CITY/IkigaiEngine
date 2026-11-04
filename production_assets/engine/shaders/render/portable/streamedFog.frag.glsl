#version 450
layout(std140,set=0,binding=0) uniform EngineUBO {
mat4 View; mat4 Projection; vec3 ViewPos; float Time;
vec2 ViewportSize; float FPS; int FrameCount;
} engine_UBO;
layout(std140,set=0,binding=3) uniform FrameData {
mat4 InvViewProjection; mat4 PreviousViewProjection; mat4 PreviousModel;
vec4 CameraParams; vec4 FrameState;
} engine_Frame;
layout(location=0) in vec2 UV;
layout(location=0) out vec4 Color;
layout(set=0,binding=8) uniform sampler2D Scene;
layout(set=0,binding=9) uniform sampler2D Depth;
layout(set=0,binding=10) uniform sampler2D LowDepth;
layout(set=0,binding=11) uniform sampler2D Radiance;
layout(set=0,binding=12) uniform sampler2D Transmittance;

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

void main() {
vec4 scene=texelFetch(Scene,ivec2(gl_FragCoord.xy),0);
vec3 p=worldPosition(UV,texture(Depth,UV).r);float reference=viewDepth(p);
vec2 size=vec2(textureSize(Radiance,0)),coord=UV*size-.5,cell=floor(coord),f=fract(coord);
float sum=0,T=0;vec3 L=vec3(0);bool neutral=true;
for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
ivec2 pixel=clamp(ivec2(cell)+ivec2(x,y),ivec2(0),textureSize(Radiance,0)-1);
vec2 uv=(vec2(pixel)+.5)/size;float low=viewDepth(worldPosition(uv,texture(LowDepth,uv).r));
float w=(x==0?1-f.x:f.x)*(y==0?1-f.y:f.y)*exp(-abs(reference-low)/max(.05,reference*.02));
vec3 l=texelFetch(Radiance,pixel,0).rgb;float t=texelFetch(Transmittance,pixel,0).r;
neutral=neutral && all(equal(l,vec3(0))) && t==1;sum+=w;L+=l*w;T+=t*w;
} if(neutral) {Color=scene;return;}
if(sum>1e-6) {L/=sum;T/=sum;}else {L=texture(Radiance,UV).rgb;T=texture(Transmittance,UV).r;}
Color=vec4(scene.rgb*clamp(T,0,1)+L,scene.a);
}
