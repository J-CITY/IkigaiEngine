#ifndef IKIGAI_SURFACE_WEATHER_GLSL
#define IKIGAI_SURFACE_WEATHER_GLSL
#include "environment.glsl"
// Fragment counterpart of SurfaceState; vertex-only helpers live in deformation.glsl.
layout(std140,set=0,binding=41) uniform SurfaceState {
    vec4 Parameters; vec4 Shape; vec4 Fur; vec4 Pivot;
} engine_Surface;
layout(std140,set=0,binding=47) uniform SurfaceWeatherState {
    vec4 Grid; // origin xz, cell size, enabled
    vec4 Projection; // precipitation slope xz, reference height, grid dimension
    vec4 Cells[256]; // first-hit height, wetness, snow, known
} engine_SurfaceWeather;
layout(location=6) in float SurfaceShell;
layout(location=7) in vec4 InstanceVariation;
float surfaceNoise(vec2 p) { return fract(sin(dot(floor(p),vec2(127.1,311.7)))*43758.5453); }
void surfaceCoverage(vec2 uv) {
    float dither=fract(52.9829189*fract(dot(floor(gl_FragCoord.xy),vec2(.06711056,.00583715))));
    if (InstanceVariation.w < 1.0 && dither >= InstanceVariation.w) discard;
    if(engine_Surface.Fur.w>.5 && engine_Surface.Parameters.x>.5 && engine_Surface.Parameters.x<1.5) {
        float width=mix(.43,.025,smoothstep(.1,1,uv.y));
        if(abs(uv.x-.5)>width) discard;
    }
    if(engine_Surface.Fur.w>.5 && engine_Surface.Parameters.x>1.5 && engine_Surface.Parameters.x<2.5) {
        if(length((uv-.5)*2)>1) discard;
    }
    if(engine_Surface.Parameters.x>2.5 && SurfaceShell>.001) {
        float strand=surfaceNoise(uv*350);
        if(strand>engine_Surface.Fur.z*(1-SurfaceShell*.75)) discard;
    }
}
vec3 surfaceWeatherCell(ivec2 index,vec3 world) {
    int dimension=int(engine_SurfaceWeather.Projection.w);
    index=clamp(index,ivec2(0),ivec2(dimension-1));
    vec4 cell=engine_SurfaceWeather.Cells[index.y*dimension+index.x];
    float exposure=cell.w*smoothstep(cell.x-.25,cell.x-.05,world.y);
    return vec3(cell.yz,exposure);
}
vec2 surfaceWeatherAmount(vec3 world) {
    vec2 global=engine_Environment.WeatherState.zw;
    if(engine_SurfaceWeather.Grid.w<.5) return global;
    vec2 projected=world.xz+engine_SurfaceWeather.Projection.xy*(world.y-engine_SurfaceWeather.Projection.z);
    vec2 coordinate=(projected-engine_SurfaceWeather.Grid.xy)/engine_SurfaceWeather.Grid.z-.5;
    float dimension=engine_SurfaceWeather.Projection.w;
    float border=min(min(coordinate.x,coordinate.y),min(dimension-1-coordinate.x,dimension-1-coordinate.y));
    if(border<=0) return global;
    ivec2 index=ivec2(floor(coordinate));
    vec2 blend=fract(coordinate);
    vec3 a=surfaceWeatherCell(index,world),b=surfaceWeatherCell(index+ivec2(1,0),world);
    vec3 c=surfaceWeatherCell(index+ivec2(0,1),world),d=surfaceWeatherCell(index+ivec2(1,1),world);
    // Conservatively close shelter edges instead of blending rain through a roof.
    float exposure=min(min(a.z,b.z),min(c.z,d.z));
    vec2 amount=mix(mix(a.xy,b.xy,blend.x),mix(c.xy,d.xy,blend.x),blend.y)*exposure;
    return mix(global,amount,smoothstep(0,1,border));
}
void surfaceWeather(inout vec3 color,inout float roughness,vec3 normal,vec3 world) {
    color*=InstanceVariation.rgb;
    if(!environmentEnabled()) return;
    float response=engine_Surface.Shape.w;
    vec2 amount=surfaceWeatherAmount(world);
    float wet=amount.x*response;
    float snow=amount.y*response*smoothstep(.25,.85,normal.y);
    color=mix(color,color*.65,wet);
    roughness=mix(roughness,max(.06,roughness*.3),wet);
    color=mix(color,vec3(.82,.88,.95),snow);
    roughness=mix(roughness,.85,snow);
    if(engine_Surface.Parameters.x>2.5) color*=mix(.45,1,SurfaceShell);
}
#endif
