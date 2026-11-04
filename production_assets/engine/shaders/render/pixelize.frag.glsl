#version 450
// Ported from F:/projects/_/assets_save/gl/pixelize.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    vec2 parameters;
} data_UBO;
layout(location=0) out vec4 FragColor;
layout(location=0) in vec2 TexCoords;

layout(set=0,binding=9) uniform sampler2D u_Scene;
layout(set=0,binding=8) uniform sampler2D positionTexture;

void main() {
  
  int pixelSize = int(data_UBO.parameters.x);

  vec2 texSize  = textureSize(u_Scene, 0).xy;

  vec4 position = texture(positionTexture, TexCoords);

  if (position.a <= 0.0) {

  }

  float x = int(gl_FragCoord.x) % pixelSize;
  float y = int(gl_FragCoord.y) % pixelSize;

        x = floor(pixelSize / 2.0) - x;
        y = floor(pixelSize / 2.0) - y;

        x = gl_FragCoord.x + x;
        y = gl_FragCoord.y + y;

  vec2 uv = vec2(x, y) / texSize;

  FragColor = texture(u_Scene, uv);
}
