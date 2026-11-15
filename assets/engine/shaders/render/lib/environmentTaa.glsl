layout(std140,set=0,binding=0) uniform EngineUBO {
mat4 View; mat4 Projection; vec3 ViewPos; float Time;
vec2 ViewportSize; float FPS; int FrameCount;
} engine_UBO;
layout(std140,set=0,binding=3) uniform FrameData {
mat4 InvViewProjection; mat4 PreviousViewProjection; mat4 PreviousModel;
vec4 CameraParams; vec4 FrameState;
} engine_Frame;
layout(location=0) in vec2 UV;
layout(location=0) out vec4 Color;
layout(set=0,binding=8) uniform sampler2D Current;
layout(set=0,binding=9) uniform sampler2D History;
layout(set=0,binding=10) uniform sampler2D Velocity;
layout(set=0,binding=11) uniform sampler2D Depth;
layout(set=0,binding=12) uniform sampler2D PreviousDepth;
#ifdef IKIGAI_WEATHER_REACTIVE
layout(set=0,binding=13) uniform sampler2D Reactive;
layout(set=0,binding=14) uniform sampler2D PreviousReactive;
#endif
layout(std140,set=0,binding=4) uniform Parameters { float HistoryWeight; float DepthThreshold; } data_UBO;

bool inside(vec2 uv) { return all(greaterThanEqual(uv,vec2(0))) && all(lessThanEqual(uv,vec2(1))); }
void main() {
    vec3 current=texture(Current,UV).rgb; Color=vec4(current,1);
    if(engine_Frame.FrameState.x<0.5 || texture(Depth,UV).r>=.999999) return;
    vec2 previousUV=UV-texture(Velocity,UV).xy;
    if(!inside(previousUV)) return;
    vec3 low=current,high=current; vec2 texel=1.0/vec2(textureSize(Current,0));
    float reactive=0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
        vec2 offset=vec2(x,y)*texel;
        vec3 c=texture(Current,UV+offset).rgb; low=min(low,c); high=max(high,c);
#ifdef IKIGAI_WEATHER_REACTIVE
        reactive=max(reactive,max(texture(Reactive,UV+offset).r,texture(PreviousReactive,previousUV+offset).r));
#endif
    }
    float d=texture(Depth,UV).r,pd=texture(PreviousDepth,previousUV).r;
    if(abs(d-pd)>data_UBO.DepthThreshold) return;
    vec3 history=clamp(texture(History,previousUV).rgb,low,high);
    float historyWeight=clamp(data_UBO.HistoryWeight,0,0.95)*(1-clamp(reactive*4,0,1));
    Color=vec4(mix(current,history,historyWeight),1);
}
