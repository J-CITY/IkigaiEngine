#version 450
layout(std140,set=0,binding=0) uniform EngineUBO {
    mat4 View; mat4 Projection; vec3 ViewPos; float Time;
    vec2 ViewportSize; float FPS; int FrameCount;
} engine_UBO;
struct VgInstance { mat4 World; mat4 PreviousWorld; mat4 Normal; vec4 Params; };
layout(std430,set=0,binding=38) readonly buffer VgInstances { VgInstance Items[]; } engine_VgInstances;
layout(std430,set=0,binding=39) readonly buffer VgDraws { uvec4 Items[]; } engine_VgDraws;
layout(location=0) in vec3 Position;
layout(location=4) in uint DrawIndex;
layout(location=0) flat out int VisibilityDraw;

void main() {
    VisibilityDraw = int(DrawIndex);
    uint instance = engine_VgDraws.Items[DrawIndex].x;
    precise vec4 worldPosition = engine_VgInstances.Items[instance].World * vec4(Position, 1.0);
    precise mat4 viewProjection = engine_UBO.Projection * engine_UBO.View;
    precise vec4 clip = viewProjection * worldPosition;
    gl_Position = clip;
}
