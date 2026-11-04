#version 450
// Ported from F:/projects/_/assets_save/gl/blur.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    bool horizontal;
} data_UBO;
layout(location=0) out vec4 FragColor;
layout(location=0) in vec2 TexCoords;

layout(set=0,binding=8) uniform sampler2D image;

const float weight[5] = float[] (0.2270270270, 0.1945945946, 0.1216216216, 0.0540540541, 0.0162162162);

void main() {
     vec2 tex_offset = 1.0 / textureSize(image, 0); 
     vec3 result = texture(image, TexCoords).rgb * weight[0];
     if(data_UBO.horizontal)
     {
         for(int i = 1; i < 5; ++i)
         {
            result += texture(image, TexCoords + vec2(tex_offset.x * i, 0.0)).rgb * weight[i];
            result += texture(image, TexCoords - vec2(tex_offset.x * i, 0.0)).rgb * weight[i];
         }
     }
     else
     {
         for(int i = 1; i < 5; ++i)
         {
             result += texture(image, TexCoords + vec2(0.0, tex_offset.y * i)).rgb * weight[i];
             result += texture(image, TexCoords - vec2(0.0, tex_offset.y * i)).rgb * weight[i];
         }
     }
     FragColor = vec4(result, 1.0);
}