#ifndef IKIGAI_ENVIRONMENT_GLSL
#define IKIGAI_ENVIRONMENT_GLSL
layout(std140,set=0,binding=40) uniform EnvironmentState {
    vec4 SunDirection;
    vec4 SunColor;
    vec4 Atmosphere;
    vec4 CloudLayer;
    vec4 CloudShape;
    vec4 Wind; // velocity xz, gust strength, resolved sun light index + 1
    vec4 CloudOffset; // advection xy, view/light step caps zw (zero uses legacy budgets)
    vec4 PreviousCloudOffset; // previous advection xy, shadow step cap z
    vec4 WeatherState;
    vec4 Timing;
    vec4 CloudShadowMap; // center xz, half extent, valid
    vec4 AtmosphereLut; // transmittance valid, sky-view valid, reference height, aerial valid
} engine_Environment;

bool environmentEnabled() { return engine_Environment.Atmosphere.x > .5; }
vec3 environmentSun() { return engine_Environment.SunDirection.xyz; }
vec3 environmentSunRadiance() { return engine_Environment.SunColor.rgb * engine_Environment.SunDirection.w; }
bool environmentIsSunLight(int index) { return index>=0 && index+1==int(engine_Environment.Wind.w); }
int environmentCloudViewSteps(float authored) {
    int count=clamp(int(authored),8,96);
    return environmentEnabled() && engine_Environment.CloudOffset.z>0
        ? min(count,clamp(int(engine_Environment.CloudOffset.z),8,96)) : count;
}
int environmentCloudLightSteps() {
    return environmentEnabled() && engine_Environment.CloudOffset.w>0
        ? clamp(int(engine_Environment.CloudOffset.w),1,6) : 6;
}
int environmentCloudShadowSteps() {
    return environmentEnabled() && engine_Environment.PreviousCloudOffset.z>0
        ? clamp(int(engine_Environment.PreviousCloudOffset.z),1,8) : 8;
}

// Single-scattering atmosphere. World units use the same 6000-unit planet as clouds.
// Integrate density rather than using a fixed daytime gradient at sunset/night.
vec3 environmentSkyDirect(vec3 direction) {
    vec3 ray=normalize(direction), sun=environmentSun();
    float mu=clamp(dot(ray,sun),-1,1);
    float density=engine_Environment.Atmosphere.z;
    vec3 betaR=vec3(.0058,.0135,.0331)*density;
    vec3 betaM=vec3(.002*engine_Environment.Atmosphere.y)*density;
    float horizon=sqrt(max(ray.y*ray.y+.006, .006));
    float path=80.0/horizon;
    float sunPath=80.0/sqrt(max(sun.y*sun.y+.006,.006));
    float phaseR=3.0/(16.0*3.14159265)*(1+mu*mu);
    float g=.76;
    float phaseM=(1-g*g)/(4*3.14159265*pow(max(1+g*g-2*g*mu,.01),1.5));
    vec3 light=vec3(0),transmittance=vec3(1);
    float stepLength=path/12;
    for(int i=0;i<12;++i) {
        float distance=(float(i)+.5)*stepLength;
        float height=max(ray.y*distance+distance*distance/12000,0);
        float rhoR=exp(-height/80),rhoM=exp(-height/12);
        vec3 extinction=betaR*rhoR+betaM*rhoM;
        vec3 sunT=exp(-(betaR*rhoR+betaM*rhoM)*sunPath);
        light+=transmittance*sunT*(betaR*rhoR*phaseR+betaM*rhoM*phaseM)*stepLength;
        transmittance*=exp(-extinction*stepLength);
    }
    float day=engine_Environment.CloudShape.w;
    light*=environmentSunRadiance()*8;
    light+=vec3(.001,.002,.005)*(1-day)*(1-clamp(ray.y,0,1)*.4);
    float disc=cos(engine_Environment.SunColor.w);
    light+=environmentSunRadiance()*transmittance*smoothstep(disc-.00001,disc,mu);
    vec3 ground=vec3(.015,.018,.012)*(.02+day);
    return mix(ground,max(light,vec3(0)),smoothstep(-.025,.005,ray.y));
}

#include "atmosphere.glsl"
#if defined(IKIGAI_ATMOSPHERE_BUILD_TRANSMITTANCE) || defined(IKIGAI_ATMOSPHERE_BUILD_SKYVIEW) || defined(IKIGAI_ATMOSPHERE_BUILD_AERIAL)
#define IKIGAI_ATMOSPHERE_BAKING
#endif

#ifndef IKIGAI_ATMOSPHERE_BAKING
vec3 environmentSolarTransmittance(vec3 position) {
    if(!environmentEnabled() || engine_Environment.AtmosphereLut.x<.5) return vec3(1);
    vec3 planetPosition=position+vec3(0,AtmospherePlanetRadius,0);
    float radius=length(planetPosition),height=max(radius-AtmospherePlanetRadius,.01);
    float cosine=dot(planetPosition/max(radius,1),environmentSun());
    if(atmosphereGroundDistance(height,cosine)>=0) return vec3(0);
    if(height>=AtmosphereTop) {
        // An observer above the atmosphere can still see sunlight through its
        // limb. Start at the near entry instead of clamping the observer height.
        float projection=radius*cosine,topRadius=AtmospherePlanetRadius+AtmosphereTop;
        float discriminant=projection*projection+topRadius*topRadius-radius*radius;
        if(cosine>=0 || discriminant<=0) return vec3(1);
        cosine=-sqrt(discriminant)/topRadius;
        height=AtmosphereTop;
    }
    return atmosphereSampleTransmittance(height,cosine);
}
vec3 environmentSurfaceLight(vec3 position,int lightType,int lightIndex,vec3 radiance) {
    // Match the resolved Environment.Sun, including after light culling/order
    // changes. Other directional lights keep their authored radiance.
    if(lightType!=1 || !environmentIsSunLight(lightIndex)) return radiance;
    return radiance*environmentSolarTransmittance(position);
}
#endif

vec3 environmentSky(vec3 direction) {
#ifndef IKIGAI_ATMOSPHERE_BAKING
    if(engine_Environment.AtmosphereLut.y>.5) {
        vec3 ray=normalize(direction);
        vec3 light=atmosphereSkyScattering(ray);
        float alignment=dot(ray,environmentSun());
        float disc=cos(engine_Environment.SunColor.w);
        vec3 transmittance=atmosphereSampleTransmittance(engine_Environment.AtmosphereLut.z,ray.y);
        return light+environmentSunRadiance()*transmittance*smoothstep(disc-.00001,disc,alignment);
    }
#endif
    return environmentSkyDirect(direction);
}

vec3 environmentFog(vec3 color,vec3 world,vec3 camera) {
    if(!environmentEnabled()) return color;
    vec3 delta=world-camera;
    float distance=length(delta);
    if(distance<.001) return color;
#ifndef IKIGAI_ATMOSPHERE_BAKING
    if(engine_Environment.AtmosphereLut.w>.5) {
        AtmosphereSegment aerial=atmosphereAerialPerspective(delta/distance,distance);
        return color*aerial.Transmittance+aerial.Radiance;
    }
#endif
    float opacity=1-exp(-distance*engine_Environment.Atmosphere.w);
    vec3 scattering=environmentSky(delta/distance);
#ifndef IKIGAI_ATMOSPHERE_BAKING
    // The solar disc is a distant emitter, not local in-scattering in the fog.
    if(engine_Environment.AtmosphereLut.y>.5) scattering=atmosphereSkyScattering(delta/distance);
#endif
    return mix(color,scattering,clamp(opacity,0,1));
}
#endif
