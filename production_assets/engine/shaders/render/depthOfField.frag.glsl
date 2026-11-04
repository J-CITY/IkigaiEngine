#version 450
// Ported from F:/projects/_/assets_save/gl/depthOfField.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    vec3 focusPoint;
    vec2 nearFar;
} data_UBO;
layout(location=0) out vec4 FragColor;
layout(location=0) in vec2 TexCoords;

layout(set=0,binding=11) uniform sampler2D u_Scene;

layout(set=0,binding=10) uniform sampler2D positionTexture;
layout(set=0,binding=8) uniform sampler2D noiseTexture;
layout(set=0,binding=9) uniform sampler2D outOfFocusTexture;

void main() {
  float minDistance =  8.0;
  float maxDistance = 222.0;

  float far  = data_UBO.nearFar.y;

  vec2 texSize  = textureSize(u_Scene, 0).xy;

  vec4 focusColor = texture(u_Scene, TexCoords);

  FragColor = focusColor;

  vec4 position = texture(positionTexture, TexCoords);

  if (position.a <= 0) { FragColor = vec4(1.0); return; }

  vec4 outOfFocusColor = texture(outOfFocusTexture, TexCoords);
  vec4 _focusPoint      = texture(positionTexture,   data_UBO.focusPoint.xy);

  float blur =
    smoothstep
      ( minDistance
      , maxDistance
      , length(position - _focusPoint)
      );

  FragColor  = mix(focusColor, outOfFocusColor, blur);
}
