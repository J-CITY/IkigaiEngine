#version 450
layout(location=0) in vec2 UV;
layout(location=1) in vec4 Tint;
layout(location=2) flat in vec4 GlyphBounds;
layout(set=0,binding=8) uniform sampler2D Image;
layout(std140,set=0,binding=9) uniform SdfData {
    vec4 Metrics; // spread, outline in generation pixels, shadow UV offset
    vec4 Outline;
    vec4 Shadow;
} engine_Sdf;
layout(location=0) out vec4 Color;
float distanceAt(vec2 uv) {float d=(texture(Image,uv).r*255.0-128.0)/128.0*engine_Sdf.Metrics.x;return any(lessThan(uv,GlyphBounds.xy))||any(greaterThan(uv,GlyphBounds.zw))?-engine_Sdf.Metrics.x:d;}
void main() {
    float distance=distanceAt(UV);
    float aa=max(fwidth(distance)*0.5,0.001);
    float fill=smoothstep(-aa,aa,distance);
    float outline=smoothstep(-aa,aa,distance+engine_Sdf.Metrics.y);
    float shadow=smoothstep(-aa,aa,distanceAt(UV-engine_Sdf.Metrics.zw));
    float sa=shadow*engine_Sdf.Shadow.a*Tint.a;
    vec4 c=vec4(engine_Sdf.Shadow.rgb*sa,sa);
    float oa=max(outline-fill,0.0)*engine_Sdf.Outline.a*Tint.a;
    float fa=fill*Tint.a;
    Color=vec4(Tint.rgb*fa+engine_Sdf.Outline.rgb*oa,fa+oa)+c*(1.0-fa-oa);
}
