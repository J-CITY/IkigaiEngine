#version 450
// Ported from F:/projects/_/assets_save/gl/posterize.fs.glsl

layout(location=0) out vec4 FragColor;
layout(location=0) in vec2 TexCoords;

layout(set=0,binding=9) uniform sampler2D u_Scene;
layout(set=0,binding=8) uniform sampler2D positionTexture;

void main() {
  float levels = 6.0;

  vec2 texSize  = textureSize(u_Scene, 0).xy;
  vec2 texCoord = TexCoords;

  vec4 position = texture(positionTexture, texCoord);
  if (position.a <= 0) { FragColor = texture(u_Scene, texCoord); return; }

  FragColor = texture(u_Scene, texCoord);

  float greyscale = max(FragColor.r, max(FragColor.g, FragColor.b));

  float lower     = floor(greyscale * levels) / levels;
  float lowerDiff = abs(greyscale - lower);

  float upper     = ceil(greyscale * levels) / levels;
  float upperDiff = abs(upper - greyscale);

  float level      = lowerDiff <= upperDiff ? lower : upper;
  float adjustment = level / max(greyscale,1e-5);

  FragColor.rgb = FragColor.rgb * adjustment;

}
