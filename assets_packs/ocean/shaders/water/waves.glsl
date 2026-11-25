layout(std140,set=0,binding=0) uniform EngineUBO {
 mat4 View; mat4 Projection; vec3 ViewPos; float Time;
 vec2 ViewportSize; float FPS; int FrameCount;
} engine_UBO;
layout(std140,set=0,binding=3) uniform FrameData {
 mat4 InvViewProjection; mat4 PreviousViewProjection; mat4 PreviousModel;
 vec4 CameraParams; vec4 FrameState;
} engine_Frame;
layout(std140,set=0,binding=4) uniform WaterParameters {
 vec4 WaveA; vec4 WaveB; vec4 WaveC; vec4 Tint; vec4 Weather; vec4 PreviousPhases; vec4 Hull; vec4 HullMotion;
} water_;
vec3 sampleWave(vec4 wave,vec2 point) {
 float phase=dot(wave.xy,point)+wave.w;
 return vec3(wave.z*sin(phase),wave.xy*(wave.z*cos(phase)));
}
vec3 surface(vec2 point) {
 return sampleWave(water_.WaveA,point)+sampleWave(water_.WaveB,point)+sampleWave(water_.WaveC,point);
}
