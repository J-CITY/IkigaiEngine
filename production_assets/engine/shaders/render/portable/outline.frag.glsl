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
layout(set=0,binding=10) uniform sampler2D Normals;
layout(std140,set=0,binding=4) uniform Parameters { vec3 EdgeColor; float Radius; float DepthThreshold; float NormalThreshold; } data_UBO;

void main() {vec2 px=data_UBO.Radius/vec2(textureSize(Scene,0));float depth=texture(Depth,UV).r;vec3 n=texture(Normals,UV).xyz;float edge=0;
for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){vec2 uv=clamp(UV+vec2(x,y)*px,vec2(0),vec2(1));
edge=max(edge,step(data_UBO.DepthThreshold,abs(texture(Depth,uv).r-depth)));
edge=max(edge,step(data_UBO.NormalThreshold,length(texture(Normals,uv).xyz-n)));}
Color=vec4(mix(texture(Scene,UV).rgb,data_UBO.EdgeColor,edge),1);}
