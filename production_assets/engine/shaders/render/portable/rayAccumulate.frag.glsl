#version 450
layout(location=0) in vec2 UV;
layout(location=0) out vec4 Color;
layout(set=0,binding=8) uniform sampler2D Sample;
layout(set=0,binding=9) uniform sampler2D Previous;
layout(std140,set=0,binding=3) uniform FrameData {
    mat4 InvViewProjection;mat4 PreviousViewProjection;mat4 PreviousModel;vec4 CameraParams;vec4 FrameState;
} engine_Frame;
void main() {
    vec3 sampleColor=texture(Sample,UV).rgb;
    if(engine_Frame.FrameState.x<.5){Color=vec4(sampleColor,1);return;}
    vec4 previous=texture(Previous,UV);float count=min(previous.a,65535.0)+1;
    Color=vec4(previous.rgb+(sampleColor-previous.rgb)/count,count);
}
