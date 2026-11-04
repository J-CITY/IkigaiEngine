#version 450
// Ported from F:/projects/_/assets_save/gl/renderToScreen.fs.glsl

layout(set=0,binding=8) uniform sampler2D inputTexture;
layout(location=0) out vec4 color;
layout(location=0) in vec2 inUV;

void main() {
    vec3 fragPos = texture(inputTexture, inUV).rgb;
    
    color = vec4(fragPos, 1.0);
}
