// World-space voxel centers are packed with X and Y in the atlas column and Z in the row.
// The final row contains the world-space origin, voxel spacing, and baked material table.
layout(set=0,binding=16) uniform sampler2D SdfAtlas;
const int SP_SDF_MAX_STEPS=96;
const float SP_SDF_MAX_RANGE=128.0;

uvec4 spSdfBytes(ivec2 texel) {
    return uvec4(round(texelFetch(SdfAtlas,texel,0)*255.0));
}

float spSdfMetadata(int column,int size) {
    uvec4 bytes=spSdfBytes(ivec2(column,size));
    return uintBitsToFloat(bytes.x|(bytes.y<<8u)|(bytes.z<<16u)|(bytes.w<<24u));
}

uvec4 spSdfVoxel(ivec3 index,int size) {
    index=clamp(index,ivec3(0),ivec3(size-1));
    return spSdfBytes(ivec2(index.x+index.y*size,index.z));
}

float spSdfDistance(uvec4 voxel) {
    return (float(voxel.x|(voxel.y<<8u))-32768.0)/4096.0;
}

float spSdfInterpolated(vec3 p,int size) {
    vec3 grid=p-0.5;
    ivec3 base=ivec3(floor(grid));
    vec3 f=fract(grid);
    float d000=spSdfDistance(spSdfVoxel(base,size));
    float d100=spSdfDistance(spSdfVoxel(base+ivec3(1,0,0),size));
    float d010=spSdfDistance(spSdfVoxel(base+ivec3(0,1,0),size));
    float d110=spSdfDistance(spSdfVoxel(base+ivec3(1,1,0),size));
    float d001=spSdfDistance(spSdfVoxel(base+ivec3(0,0,1),size));
    float d101=spSdfDistance(spSdfVoxel(base+ivec3(1,0,1),size));
    float d011=spSdfDistance(spSdfVoxel(base+ivec3(0,1,1),size));
    float d111=spSdfDistance(spSdfVoxel(base+ivec3(1,1,1),size));
    return mix(mix(mix(d000,d100,f.x),mix(d010,d110,f.x),f.y),
        mix(mix(d001,d101,f.x),mix(d011,d111,f.x),f.y),f.z);
}

// Ray parameters and positions use voxel units. Test parallel axes separately to avoid 0/0.
bool spSdfClip(vec3 origin,vec3 direction,int size,inout float start,inout float end) {
    for(int axis=0;axis<3;++axis) {
        if(abs(direction[axis])<1e-7) {
            if(origin[axis]<0.0||origin[axis]>=float(size))return false;
            continue;
        }
        float a=-origin[axis]/direction[axis];
        float b=(float(size)-origin[axis])/direction[axis];
        start=max(start,min(a,b));
        end=min(end,max(a,b));
    }
    return start<end;
}

bool spTraceSdf(vec3 position,vec3 normal,vec3 direction,out vec3 radiance) {
    radiance=vec3(0.0);
    ivec2 atlas=textureSize(SdfAtlas,0);
    int size=atlas.y-1;
    if(size<16||size>64||atlas.x!=size*size)return false;
    vec3 minimum=vec3(spSdfMetadata(0,size),spSdfMetadata(1,size),spSdfMetadata(2,size));
    float spacing=spSdfMetadata(3,size);
    if(!(spacing>0.0)||isinf(spacing)||any(isnan(minimum))||any(isinf(minimum)))return false;
    vec3 origin=(position-minimum)/spacing+0.65*normal;
    float start=0.0;
    float end=min(engine_Frame.CameraParams.y,SP_SDF_MAX_RANGE)/spacing;
    if(!spSdfClip(origin,direction,size,start,end))return false;

    for(int step=0;step<SP_SDF_MAX_STEPS&&start<end;++step) {
        vec3 p=origin+direction*start;
        uvec4 voxel=spSdfVoxel(ivec3(floor(p)),size);
        float centerDistance=spSdfDistance(voxel);
        // The point is within sqrt(3)/2 voxels of its sample. Include quantization error.
        float safeDistance=centerDistance-0.867;
        if(safeDistance>0.125) {
            start+=safeDistance;
            continue;
        }
        // Near the surface, interpolate to suppress staircase self-hits from nearest-voxel sampling.
        if(spSdfInterpolated(p,size)>0.2) {
            start+=0.125;
            continue;
        }
        int material=4+6*int(min(voxel.z,31u));
        vec3 albedo=vec3(spSdfMetadata(material,size),spSdfMetadata(material+1,size),spSdfMetadata(material+2,size));
        vec3 emissive=vec3(spSdfMetadata(material+3,size),spSdfMetadata(material+4,size),spSdfMetadata(material+5,size));
        vec3 hitNormal=-direction;
        if(spChainEnabled(SP_CHAIN_ENVIRONMENT)) {
            const vec2 offset=vec2(0.5,0.0);
            vec3 gradient=vec3(spSdfInterpolated(p+offset.xyy,size)-spSdfInterpolated(p-offset.xyy,size),
                spSdfInterpolated(p+offset.yxy,size)-spSdfInterpolated(p-offset.yxy,size),
                spSdfInterpolated(p+offset.yyx,size)-spSdfInterpolated(p-offset.yyx,size));
            if(dot(gradient,gradient)>1e-6)hitNormal=normalize(gradient);
            // Low-cost diffuse sky approximation for the static surface.
            emissive+=albedo*textureLod(Environment,hitNormal,0.0).rgb;
        }
        radiance=clamp(emissive,vec3(0.0),vec3(65504.0));
        return true;
    }
    return false;
}
