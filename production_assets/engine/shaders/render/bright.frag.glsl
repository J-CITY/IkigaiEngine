#version 450
// Ported from F:/projects/_/assets_save/gl/bright.fs.glsl

layout(location=0) in vec2 TexCoords;
layout(location=0) out vec4 OutColor;

layout(set=0,binding=8) uniform sampler2D u_Scene;

void main() {
    vec3 color = texture(u_Scene, TexCoords).rgb;
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    if(brightness > 1.0) {
        OutColor = vec4(color, 1.0);
    }
    else {
        OutColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
}
