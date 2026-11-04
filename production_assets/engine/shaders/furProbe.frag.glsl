#version 450
layout(std140,set=0,binding=41) uniform SurfaceState { vec4 Parameters; vec4 Shape; vec4 Fur; vec4 Pivot; } engine_Surface;
#include "render/lib/pbrLayerBindings.glsl"
layout(location=0) flat in float Instance;
layout(location=0) out vec4 Color;
void main() { if(pbr_Layers.Alpha.x<0) discard; Color=vec4(engine_Surface.Fur.y,Instance,engine_Surface.Parameters.x,1); }
