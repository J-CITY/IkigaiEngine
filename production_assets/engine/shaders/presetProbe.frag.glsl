#version 450
#define IKIGAI_ATMOSPHERE_BUILD_TRANSMITTANCE
#include "render/lib/environment.glsl"
layout(std140,set=0,binding=3) uniform FrameData {
mat4 InvViewProjection; mat4 PreviousViewProjection; mat4 PreviousModel;
vec4 CameraParams; vec4 FrameState;
} engine_Frame;
layout(location=0) out vec4 Color;
void main() {
    int column=int(gl_FragCoord.x);
    if(column==0) Color=engine_Environment.CloudLayer;
    else if(column==1) Color=engine_Environment.CloudShape;
    else if(column==2) Color=engine_Environment.Atmosphere;
    else if(column==3) Color=engine_Environment.WeatherState;
    else if(column==4) Color=engine_Environment.Wind;
    else if(column==5) Color=engine_Environment.Timing;
    else Color=engine_Frame.FrameState;
}
