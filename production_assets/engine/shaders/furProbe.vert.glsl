#version 450
layout(std140,set=0,binding=41) uniform SurfaceState { vec4 Parameters; vec4 Shape; vec4 Fur; vec4 Pivot; } engine_Surface;
layout(push_constant) uniform Model { mat4 model; } engine_Model;
layout(location=0) flat out float Instance;
void main() {
vec2 positions[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
gl_Position=engine_Surface.Parameters.x>2.5 ? vec4(positions[gl_VertexIndex%3],0,1) : vec4(2,2,2,1);
gl_Position.w=engine_Model.model[3][3];
Instance=float(gl_InstanceIndex);
}
