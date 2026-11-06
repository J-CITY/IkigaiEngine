// Direct lighting of a ray-traced surface point: Lambert + GGX for every engine light, shadowed through
// rtLightVisibility, which the including shader defines (trace ray in a ray generation shader, ray query in compute).
// Shared by hybrid.rgen (shadeHit) and the screen probe ray query trace.
struct Light {vec3 pos;float cutoff;vec3 forward;float outerCutoff;vec3 color;float constant;int type;float linear;float quadratic;float intensity;float radius;int shadows;float nearPlane,farPlane;};
layout(std430,set=0,binding=3) readonly buffer Lights {Light lights[];} engine_Lights;
float rtLightVisibility(vec3 p,vec3 n,vec3 l,float distance);
float rtLightAttenuation(Light light,vec3 p,out vec3 l,out float d) {
    vec3 delta=light.pos-p;d=max(length(delta),.001);l=light.type==1?normalize(-light.forward):delta/d;
    float attenuation=light.type==1?1:1/max(light.constant+light.linear*d+light.quadratic*d*d,.001);
    if(light.type==2){float outer=cos(radians(light.outerCutoff)),inner=cos(radians(light.cutoff));attenuation*=clamp((dot(-l,normalize(light.forward))-outer)/max(inner-outer,.0001),0,1);}
    return attenuation;
}
vec3 rtAccumulateDiffuseLight(vec3 result,vec3 p,vec3 n,vec3 albedo) {
    for(int i=0;i<engine_Lights.lights.length();++i) {
        Light light=engine_Lights.lights[i];if(light.type<0||light.type>2||light.intensity<=0)continue;
        vec3 l;float d,attenuation=rtLightAttenuation(light,p,l,d);
        float nl=max(dot(n,l),0);if(nl<=0||attenuation<=0)continue;
        float shadow=light.shadows!=0?rtLightVisibility(p,n,l,light.type==1?10000:d-.002):1;
        result+=albedo/3.14159265*light.color*light.intensity*attenuation*nl*shadow;
    }
    return result;
}
// Adds the reflected direct light towards v to `result` (p, n: surface; a: linear albedo).
vec3 rtAccumulateDirectLight(vec3 result,vec3 p,vec3 n,vec3 v,vec3 a,float rough,float metal) {
    vec3 f0=mix(vec3(.04),a,metal);
    for(int i=0;i<engine_Lights.lights.length();++i) {
        Light light=engine_Lights.lights[i];if(light.type<0||light.type>2||light.intensity<=0)continue;
        vec3 l;float d,attenuation=rtLightAttenuation(light,p,l,d);
        float nl=max(dot(n,l),0),nv=max(dot(n,v),.0001);if(nl<=0||attenuation<=0)continue;
        vec3 h=normalize(l+v),f=f0+(1-f0)*pow(1-max(dot(h,v),0),5);
        float a2=pow(rough,4),nh=max(dot(n,h),0),den=nh*nh*(a2-1)+1;
        float distribution=a2/max(3.14159265*den*den,.00001),k=(rough+1)*(rough+1)/8;
        float g=nv/(nv*(1-k)+k)*nl/(nl*(1-k)+k);
        float shadow=light.shadows!=0?rtLightVisibility(p,n,l,light.type==1?10000:d-.002):1;
        result+=((1-f)*a*(1-metal)/3.14159265+distribution*g*f/(4*nv*max(nl,.0001)))*light.color*light.intensity*attenuation*nl*shadow;
    }
    return result;
}
