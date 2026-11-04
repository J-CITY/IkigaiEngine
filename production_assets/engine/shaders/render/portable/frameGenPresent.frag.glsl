#version 450
layout(std140,set=0,binding=3) uniform FrameData {
mat4 InvViewProjection; mat4 PreviousViewProjection; mat4 PreviousModel;
vec4 CameraParams; vec4 FrameState;
} engine_Frame;
layout(location=0) in vec2 UV;
layout(set=0,binding=8) uniform sampler2D Scene;
layout(set=0,binding=9) uniform sampler2D UI;
layout(std140,set=0,binding=4) uniform FGParameters {int CompositeUI;} data_UBO;
layout(location=0) out vec4 Color;
void main(){vec4 scene=texture(Scene,UV),ui=texture(UI,UV);Color=vec4(data_UBO.CompositeUI!=0?ui.rgb+(1-ui.a)*scene.rgb:scene.rgb,1);}
