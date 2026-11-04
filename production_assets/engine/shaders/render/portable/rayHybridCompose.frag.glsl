#version 450
layout(location=0) in vec2 UV;layout(location=0) out vec4 Color;
layout(set=0,binding=8) uniform sampler2D Scene;
layout(set=0,binding=9) uniform sampler2D Reflection;
layout(set=0,binding=10) uniform sampler2D Indirect;
layout(set=0,binding=11) uniform sampler2D Specular;
layout(set=0,binding=12) uniform sampler2D Diffuse;
layout(set=0,binding=13) uniform sampler2D Weight;
layout(set=0,binding=14) uniform sampler2D Albedo;
layout(set=0,binding=15) uniform sampler2D Normals;
layout(set=0,binding=16) uniform sampler2D Depth;
layout(set=0,binding=17) uniform sampler2D Roughness;
layout(std140,set=0,binding=0) uniform EngineUBO {mat4 View;mat4 Projection;vec3 ViewPos;float Time;vec2 ViewportSize;float FPS;int FrameCount;} engine_UBO;
layout(std140,set=0,binding=3) uniform FrameData {mat4 InvViewProjection;mat4 PreviousViewProjection;mat4 PreviousModel;vec4 CameraParams;vec4 FrameState;} engine_Frame;
layout(std140,set=0,binding=4) uniform Parameters {int ReflectionIsRT;int IndirectIsRT;float IndirectWeight;} data_UBO;
void main() {
    vec3 scene=texture(Scene,UV).rgb;vec4 reflection=texture(Reflection,UV),indirect=texture(Indirect,UV),a=texture(Albedo,UV);
    float rc=clamp(reflection.a,0,1),gc=clamp(indirect.a,0,1)*clamp(data_UBO.IndirectWeight,0,1);
    vec3 reflected=data_UBO.ReflectionIsRT!=0?reflection.rgb*rc:reflection.rgb*texture(Weight,UV).rgb;
    vec3 response=a.rgb*(1-a.a)/3.14159265;
    if(data_UBO.IndirectIsRT!=0) {
        vec2 uv=UV;uv.y=mix(uv.y,1-uv.y,engine_Frame.CameraParams.w);float d=texture(Depth,UV).r;
        vec4 p=engine_Frame.InvViewProjection*vec4(uv*2-1,mix(d*2-1,d,engine_Frame.CameraParams.z),1);
        vec3 v=normalize(engine_UBO.ViewPos-p.xyz/p.w),n=normalize(texture(Normals,UV).xyz);
        float rough=texture(Roughness,UV).r;vec3 f0=mix(vec3(.04),a.rgb,a.a);
        vec3 f=f0+(max(vec3(1-rough),f0)-f0)*pow(1-max(dot(n,v),0),5);
        response=a.rgb*(1-a.a)*(1-f);
    }
    Color=vec4(max(scene-texture(Specular,UV).rgb*rc-texture(Diffuse,UV).rgb*gc+reflected+indirect.rgb*response*gc,vec3(0)),1);
}
