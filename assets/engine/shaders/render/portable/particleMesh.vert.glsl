#version 450
layout(location=0) in vec3 Position;
layout(location=8) in vec2 TexCoord;
layout(location=1) in vec3 Origin;
layout(location=2) in vec3 Right;
layout(location=3) in vec3 Up;
layout(location=4) in vec4 VertexColor;
layout(location=5) in vec4 Region;
layout(location=7) in vec3 Forward;
layout(std140,set=0,binding=5) uniform ParticleData { mat4 Projection; } engine_Particles;
layout(location=0) out vec2 UV;
layout(location=1) out vec4 Tint;
void main() {
    // Imported mesh UVs follow the model loader convention (default FLIP_UVS).
    UV=Region.xy+vec2(0,Region.w)+TexCoord*Region.zw*vec2(1,-1);
    Tint=VertexColor;
    gl_Position=engine_Particles.Projection*vec4(Origin+Right*Position.x+Up*Position.y+Forward*Position.z,1);
}
