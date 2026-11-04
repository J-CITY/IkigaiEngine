#ifndef IKIGAI_ATMOSPHERE_GLSL
#define IKIGAI_ATMOSPHERE_GLSL
// Same world-unit planet as the cloud layer. Single Rayleigh/Mie scattering;
// no multiple scattering, ozone or ground bounce is implied by these tables.
const float AtmospherePlanetRadius=6000.0;
const float AtmosphereTop=400.0;
const float AtmospherePi=3.14159265359;

vec3 atmosphereRayleigh(float height) {
    // Scale coefficients with the enlarged 80/12-unit density heights; using
    // unscaled kilometre coefficients here makes even the noon sky orange.
    return vec3(.00058,.00135,.00331)*engine_Environment.Atmosphere.z*exp(-max(height,0)/80.0);
}
vec3 atmosphereMie(float height) {
    return vec3(.0002*engine_Environment.Atmosphere.y*engine_Environment.Atmosphere.z*exp(-max(height,0)/12.0));
}
float atmosphereTopDistance(float height,float cosine) {
    float radius=AtmospherePlanetRadius+height;
    float projection=radius*cosine;
    float topRadius=AtmospherePlanetRadius+AtmosphereTop;
    return max(0,-projection+sqrt(max(0,projection*projection+topRadius*topRadius-radius*radius)));
}
float atmosphereGroundDistance(float height,float cosine) {
    float radius=AtmospherePlanetRadius+height;
    float projection=radius*cosine;
    float discriminant=projection*projection+AtmospherePlanetRadius*AtmospherePlanetRadius-radius*radius;
    if(cosine>=0 || discriminant<0) return -1;
    return max(0,-projection-sqrt(max(discriminant,0)));
}
vec2 atmosphereTexelUv(vec2 coordinates,ivec2 size) {
    return (clamp(coordinates,vec2(0),vec2(1))*vec2(size-1)+.5)/vec2(size);
}
vec2 atmosphereCoordinates(vec2 uv,ivec2 size) {
    return clamp((uv*vec2(size)-.5)/vec2(size-1),vec2(0),vec2(1));
}
vec3 atmosphereTransmittanceDirect(float height,float cosine) {
    if(atmosphereGroundDistance(height,cosine)>=0) return vec3(0);
    float path=atmosphereTopDistance(height,cosine);
    float radius=AtmospherePlanetRadius+height;
    vec3 opticalDepth=vec3(0);
    for(int i=0;i<40;++i) {
        float a=float(i)/40.0,b=float(i+1)/40.0;
        float start=path*a*a,finish=path*b*b,distance=(start+finish)*.5;
        float sampleHeight=sqrt(max(0,radius*radius+distance*distance+2*radius*cosine*distance))-AtmospherePlanetRadius;
        opticalDepth+=(atmosphereRayleigh(sampleHeight)+atmosphereMie(sampleHeight))*(finish-start);
    }
    return exp(-opticalDepth);
}

#ifndef IKIGAI_ATMOSPHERE_BUILD_TRANSMITTANCE
layout(set=0,binding=48) uniform sampler2D engine_AtmosphereTransmittance;
vec3 atmosphereSampleTransmittance(float height,float cosine) {
    // Test the planet analytically to avoid light leaking across its silhouette
    // when adjacent LUT texels straddle the ground intersection.
    if(atmosphereGroundDistance(height,cosine)>=0) return vec3(0);
    if(engine_Environment.AtmosphereLut.x<.5) return atmosphereTransmittanceDirect(height,cosine);
    vec2 coordinates=vec2(cosine*.5+.5,sqrt(clamp(height/AtmosphereTop,0,1)));
    return texture(engine_AtmosphereTransmittance,atmosphereTexelUv(coordinates,textureSize(engine_AtmosphereTransmittance,0))).rgb;
}

vec3 atmosphereIntegrateMedium(vec3 ray,vec3 sun,float height,float path,float densityScale,out vec3 transmittance) {
    float radius=AtmospherePlanetRadius+height;
    vec3 origin=vec3(0,radius,0);
    float mu=clamp(dot(ray,sun),-1,1),g=.76;
    float phaseR=3.0/(16.0*AtmospherePi)*(1+mu*mu);
    float phaseM=(1-g*g)/(4*AtmospherePi*pow(max(1+g*g-2*g*mu,.01),1.5));
    vec3 light=vec3(0);
    transmittance=vec3(1);
    for(int i=0;i<32;++i) {
        float a=float(i)/32.0,b=float(i+1)/32.0;
        float start=path*a*a,finish=path*b*b,stepLength=finish-start;
        vec3 position=origin+ray*((start+finish)*.5);
        float sampleRadius=length(position),sampleHeight=max(sampleRadius-AtmospherePlanetRadius,0);
        vec3 betaR=atmosphereRayleigh(sampleHeight)*densityScale,betaM=atmosphereMie(sampleHeight)*densityScale;
        vec3 extinction=betaR+betaM,stepTransmittance=exp(-extinction*stepLength);
        vec3 sunTransmittance=atmosphereSampleTransmittance(sampleHeight,dot(position/sampleRadius,sun));
        vec3 source=sunTransmittance*(betaR*phaseR+betaM*phaseM)*environmentSunRadiance()*8;
        light+=transmittance*source*(1-stepTransmittance)/max(extinction,vec3(1e-8));
        transmittance*=stepTransmittance;
    }
    return light;
}
vec3 atmosphereCanonicalSun() {
    return vec3(sqrt(max(0,1-environmentSun().y*environmentSun().y)),environmentSun().y,0);
}
float atmosphereSegmentDistance(vec3 ray,float distance) {
    float height=engine_Environment.AtmosphereLut.z;
    float path=min(max(distance,0),atmosphereTopDistance(height,ray.y));
    float ground=atmosphereGroundDistance(height,ray.y);
    return ground>=0 ? min(path,ground) : path;
}
vec3 atmosphereIntegrateSky(vec3 ray) {
    float height=engine_Environment.AtmosphereLut.z;
    float ground=atmosphereGroundDistance(height,ray.y);
    float path=atmosphereSegmentDistance(ray,100000);
    vec3 transmittance;
    vec3 light=atmosphereIntegrateMedium(ray,atmosphereCanonicalSun(),height,path,1,transmittance);
    float day=engine_Environment.CloudShape.w;
    if(ground>=0) light+=transmittance*vec3(.015,.018,.012)*(.02+day);
    light+=vec3(.001,.002,.005)*(1-day)*(1-clamp(ray.y,0,1)*.4);
    return max(light,vec3(0));
}

vec3 atmosphereSkyDirection(vec2 coordinates) {
    float elevationCoordinate=coordinates.y*2-1;
    float elevation=sign(elevationCoordinate)*elevationCoordinate*elevationCoordinate*AtmospherePi*.5;
    float azimuth=coordinates.x*AtmospherePi;
    return vec3(cos(elevation)*cos(azimuth),sin(elevation),cos(elevation)*sin(azimuth));
}
vec2 atmosphereSkyCoordinates(vec3 ray) {
    float elevation=asin(clamp(ray.y,-1,1));
    float horizontal=length(ray.xz),sunHorizontal=length(environmentSun().xz);
    float azimuth=horizontal>1e-5 && sunHorizontal>1e-5
        ? acos(clamp(dot(ray.xz,environmentSun().xz)/(horizontal*sunHorizontal),-1,1)) : 0;
    float elevationCoordinate=sign(elevation)*sqrt(abs(elevation)/(AtmospherePi*.5));
    return vec2(azimuth/AtmospherePi,elevationCoordinate*.5+.5);
}
#ifndef IKIGAI_ATMOSPHERE_BUILD_SKYVIEW
layout(set=0,binding=49) uniform sampler2D engine_AtmosphereSkyView;
vec3 atmosphereSkyScattering(vec3 ray) {
    vec2 coordinates=atmosphereSkyCoordinates(ray);
    return texture(engine_AtmosphereSkyView,atmosphereTexelUv(coordinates,textureSize(engine_AtmosphereSkyView,0))).rgb;
}
#endif

// A 32 x 64 x 24 directional/depth volume, packed into one 768 x 128 atlas.
// Upper band: in-scattering RGB. Lower band: spectral transmittance RGB.
// Packing both bands avoids an extra sampler; material layer limits still apply.
const int AtmosphereAerialAzimuth=32;
const int AtmosphereAerialElevation=64;
const int AtmosphereAerialDepth=24;
const float AtmosphereAerialMaxDistance=8000.0;
float atmosphereAerialDistance(float slice) {
    return 4*(exp2(slice/float(AtmosphereAerialDepth-1)*log2(1+AtmosphereAerialMaxDistance/4))-1);
}
struct AtmosphereSegment {
    vec3 Radiance;
    vec3 Transmittance;
};
AtmosphereSegment atmosphereIntegrateAerial(vec3 ray,float distance) {
    AtmosphereSegment result;
    float scale=max(engine_Environment.Atmosphere.w,0)/.0003;
    result.Radiance=atmosphereIntegrateMedium(ray,atmosphereCanonicalSun(),engine_Environment.AtmosphereLut.z,
        atmosphereSegmentDistance(ray,distance),scale,result.Transmittance);
    result.Radiance+=vec3(.001,.002,.005)*(1-engine_Environment.CloudShape.w)*(1-result.Transmittance);
    return result;
}
#if !defined(IKIGAI_ATMOSPHERE_BUILD_AERIAL) && !defined(IKIGAI_ATMOSPHERE_BUILD_SKYVIEW)
layout(set=0,binding=50) uniform sampler2D engine_AtmosphereAerial;
vec3 atmosphereAerialTexel(vec2 coordinates,float slice,float band) {
    vec2 pixel=vec2(slice*float(AtmosphereAerialAzimuth)+coordinates.x*float(AtmosphereAerialAzimuth-1),
        band*float(AtmosphereAerialElevation)+coordinates.y*float(AtmosphereAerialElevation-1));
    return texture(engine_AtmosphereAerial,(pixel+.5)/vec2(textureSize(engine_AtmosphereAerial,0))).rgb;
}
AtmosphereSegment atmosphereAerialPerspective(vec3 ray,float distance) {
    vec2 coordinates=atmosphereSkyCoordinates(ray);
    float slice=log2(1+clamp(distance,0,AtmosphereAerialMaxDistance)/4)/log2(1+AtmosphereAerialMaxDistance/4)*float(AtmosphereAerialDepth-1);
    float first=floor(slice),last=min(first+1,float(AtmosphereAerialDepth-1)),weight=fract(slice);
    AtmosphereSegment result;
    result.Radiance=mix(atmosphereAerialTexel(coordinates,first,0),atmosphereAerialTexel(coordinates,last,0),weight);
    result.Transmittance=clamp(mix(atmosphereAerialTexel(coordinates,first,1),atmosphereAerialTexel(coordinates,last,1),weight),0,1);
    return result;
}
#endif
#endif
#endif
