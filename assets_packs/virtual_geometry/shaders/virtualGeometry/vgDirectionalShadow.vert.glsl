#version 450
layout(location=0) in vec3 Position;
layout(std140,set=0,binding=5) uniform ShadowPass { mat4 ViewProjection; } engine_ShadowPass;
layout(push_constant) uniform Model { mat4 model; } engine_Model;

void main() {
    gl_Position = engine_ShadowPass.ViewProjection * engine_Model.model * vec4(Position, 1.0);
}
