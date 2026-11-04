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
layout(set=0,binding=9) uniform sampler2D Layer0;
layout(set=0,binding=10) uniform sampler2D Layer1;
layout(set=0,binding=11) uniform sampler2D Layer2;
layout(set=0,binding=12) uniform sampler2D Layer3;
layout(set=0,binding=13) uniform sampler2D Overflow;
layout(std140,set=0,binding=4) uniform Parameters { bool ShowOverflow; } data_UBO;
void main() {vec4 scene=texelFetch(Scene,ivec2(gl_FragCoord.xy),0);
vec4 layer3=texelFetch(Layer3,ivec2(gl_FragCoord.xy),0);scene.rgb=mix(scene.rgb,layer3.rgb,clamp(layer3.a,0,1));
vec4 layer2=texelFetch(Layer2,ivec2(gl_FragCoord.xy),0);scene.rgb=mix(scene.rgb,layer2.rgb,clamp(layer2.a,0,1));
vec4 layer1=texelFetch(Layer1,ivec2(gl_FragCoord.xy),0);scene.rgb=mix(scene.rgb,layer1.rgb,clamp(layer1.a,0,1));
vec4 layer0=texelFetch(Layer0,ivec2(gl_FragCoord.xy),0);scene.rgb=mix(scene.rgb,layer0.rgb,clamp(layer0.a,0,1));
if(data_UBO.ShowOverflow && texelFetch(Overflow,ivec2(gl_FragCoord.xy),0).w>0) scene=vec4(1,0,1,1);
Color=scene;}
