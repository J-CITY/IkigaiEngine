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
layout(set=0,binding=9) uniform sampler2D Rays;
layout(set=0,binding=10) uniform sampler2D Mask;
layout(std140,set=0,binding=4) uniform Parameters { float Strength; vec4 Tint; } data_UBO;

void main() {vec4 scene=texelFetch(Scene,ivec2(gl_FragCoord.xy),0);if(data_UBO.Strength<=0) {Color=scene;return;}
Color=vec4(scene.rgb+max(data_UBO.Tint.rgb,vec3(0))*data_UBO.Strength*(texture(Rays,UV).rgb+texture(Mask,UV).rgb),scene.a);}
