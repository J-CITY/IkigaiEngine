#version 450
// Ported from F:/projects/_/assets_save/gl/dilation.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    vec2 parameters;
} data_UBO;
layout(location=0) out vec4 FragColor;
layout(location=0) in vec2 TexCoords;

layout(set=0,binding=8) uniform sampler2D u_Scene;

void main() {
  int   size         = int(data_UBO.parameters.x);
  float separation   =     data_UBO.parameters.y;
  float minThreshold = 0.2;
  float maxThreshold = 0.5;

  vec2 texSize   = textureSize(u_Scene, 0).xy;

  FragColor = texture(u_Scene, TexCoords);
  vec2 fragCoord = gl_FragCoord.xy;

  if (size <= 0) { return; }

  float  mx = 0.0;
  vec4  cmx = FragColor;

  for (int i = -size; i <= size; ++i) {
    for (int j = -size; j <= size; ++j) {

      if (!(distance(vec2(i, j), vec2(0, 0)) <= size)) { continue; }

      vec4 c =
        texture
          ( u_Scene
          ,   ( gl_FragCoord.xy
              + (vec2(i, j) * separation)
              )
            / texSize
          );

      float mxt = dot(c.rgb, vec3(0.3, 0.59, 0.11));

      if (mxt > mx) {
         mx = mxt;
        cmx = c;
      }
    }
  }

  FragColor.rgb =
    mix
      ( FragColor.rgb
      , cmx.rgb
      , smoothstep(minThreshold, maxThreshold, mx)
      );
}