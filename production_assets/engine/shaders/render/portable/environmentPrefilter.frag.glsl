#version 450
layout(std140,set=0,binding=0) uniform EngineUBO {
mat4 View; mat4 Projection; vec3 ViewPos; float Time;
vec2 ViewportSize; float FPS; int FrameCount;
} engine_UBO;
layout(std140,set=0,binding=3) uniform FrameData {
mat4 InvViewProjection; mat4 PreviousViewProjection; mat4 PreviousModel;
vec4 CameraParams; vec4 FrameState;
} engine_Frame;
layout(set=0,binding=8) uniform samplerCube Environment;
layout(location=0) in vec2 UV;
layout(location=0) out vec4 Color;
layout(std140,set=0,binding=4) uniform Parameters { int Face; int Samples; float Roughness; } data_UBO;

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

const float PI=3.14159265359;
vec2 hammersley(uint i,uint n) {return vec2(float(i)/float(n),float(bitfieldReverse(i))*2.3283064365386963e-10);}
mat3 tangentBasis(vec3 n) {vec3 t=normalize(cross(abs(n.y)<.95?vec3(0,1,0):vec3(1,0,0),n));return mat3(t,cross(n,t),n);}
vec3 importanceGGX(vec2 xi,vec3 n,float rough) {
float alpha=max(rough*rough,.0001),a2=alpha*alpha;
float phi=2*PI*xi.x,c=sqrt((1-xi.y)/max(1+(a2-1)*xi.y,1e-6));
return normalize(tangentBasis(n)*vec3(cos(phi)*sqrt(max(1-c*c,0)),sin(phi)*sqrt(max(1-c*c,0)),c));
}

void main() {
vec3 n=faceDirection(canonicalUV(UV),data_UBO.Face),v=n,sum=vec3(0);float weights=0;
float rough=clamp(data_UBO.Roughness,0,1);
if(rough<.001) {Color=vec4(textureLod(Environment,n,0).rgb,1);return;}
uint count=uint(clamp(data_UBO.Samples,16,1024));
float resolution=float(textureSize(Environment,0).x),texelSolidAngle=4*PI/(6*resolution*resolution);
for(uint i=0;i<count;++i) {
vec3 h=importanceGGX(hammersley(i,count),n,rough),l=normalize(2*dot(v,h)*h-v);
float nl=max(dot(n,l),0);if(nl<=0) continue;
float nh=max(dot(n,h),0),vh=max(dot(v,h),1e-5),a2=pow(rough,4),denom=nh*nh*(a2-1)+1;
float distribution=a2/max(PI*denom*denom,1e-8),pdf=max(distribution*nh/(4*vh),1e-6);
float sampleSolidAngle=1/(float(count)*pdf);
float lod=clamp(.5*log2(sampleSolidAngle/texelSolidAngle),0,float(textureQueryLevels(Environment)-1));
sum+=textureLod(Environment,l,lod).rgb*nl;weights+=nl;
} Color=vec4(sum/max(weights,1e-6),1);
}
