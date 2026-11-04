#version 450
// Ported from F:/projects/_/assets_save/gl/filmGrain.fs.glsl

layout(location=0) out vec4 FragColor;
layout(location=0) in vec2 TexCoords;

layout(std140,set=0,binding=0) uniform EngineUBO {
    mat4    View;
    mat4    Projection;
    vec3    ViewPos;
    float   Time;
    vec2    ViewportSize;
    float    FPS;
    int      FrameCount;
} engine_UBO;

layout(push_constant) uniform ModelPush { mat4 model; } engine_Model;

layout(set=0,binding=8) uniform sampler2D u_Scene;

#define PI 3.1415926538

void main() {
  float amount = 0.1;

  vec2 texSize  = textureSize(u_Scene, 0).xy;
  
  vec4 color = texture(u_Scene, TexCoords);

    float randomIntensity =
      fract
        ( 10000
        * sin
            (
              ( gl_FragCoord.x
              + gl_FragCoord.y
              * engine_UBO.Time
              )
            * PI / 180.0
            )
        );

    amount *= randomIntensity;

    color.rgb += amount;

  FragColor = color;
}