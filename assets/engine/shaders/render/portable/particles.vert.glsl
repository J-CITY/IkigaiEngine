#version 450
layout(location=0) in vec2 Corner;
layout(location=1) in vec3 Origin;
layout(location=2) in vec3 Right;
layout(location=3) in vec3 Up;
layout(location=4) in vec4 VertexColor;
layout(location=5) in vec4 Region;
layout(location=6) in vec2 Pivot;
layout(std140,set=0,binding=5) uniform ParticleData { mat4 Projection; } engine_Particles;
layout(location=0) out vec2 UV;
layout(location=1) out vec4 Tint;
layout(location=2) out vec3 WorldPosition;
void main() {
    UV=Region.xy+Corner*Region.zw;
    Tint=VertexColor;
    WorldPosition=Origin+Right*(Corner.x-Pivot.x)+Up*(Corner.y-Pivot.y);
    gl_Position=engine_Particles.Projection*vec4(WorldPosition,1);
}
