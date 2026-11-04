#ifndef IKIGAI_INSTANCES_GLSL
#define IKIGAI_INSTANCES_GLSL
struct InstanceTransform { mat4 Current; mat4 Previous; vec4 Tint; };
layout(std430,set=0,binding=36) readonly buffer InstanceTransforms {
    InstanceTransform Transforms[];
} engine_InstanceTransforms;
layout(std140,set=0,binding=37) uniform InstanceMeta {
    vec4 Parameters; // instance count, fur shell count, reserved
    vec4 Tint;
} engine_InstanceMeta;
layout(location=7) out vec4 InstanceVariation;
vec4 instanceVariation() {
    if (engine_InstanceMeta.Parameters.x < 0.5) return engine_InstanceMeta.Tint;
    int index = gl_InstanceIndex / max(int(engine_InstanceMeta.Parameters.y), 1);
    return engine_InstanceTransforms.Transforms[index].Tint;
}
mat4 instanceModel() {
    if (engine_InstanceMeta.Parameters.x < 0.5) return engine_Model.model;
    int index = gl_InstanceIndex / max(int(engine_InstanceMeta.Parameters.y), 1);
    return engine_InstanceTransforms.Transforms[index].Current;
}
mat4 instancePreviousModel() {
    if (engine_InstanceMeta.Parameters.x < 0.5) return engine_Frame.PreviousModel;
    int index = gl_InstanceIndex / max(int(engine_InstanceMeta.Parameters.y), 1);
    return engine_InstanceTransforms.Transforms[index].Previous;
}
#endif
