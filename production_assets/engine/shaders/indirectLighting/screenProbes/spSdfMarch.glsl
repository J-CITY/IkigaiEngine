// World-space voxel centers are packed with X and Y in the atlas column and Z in the row.
// The final row contains the world-space origin, voxel spacing, and baked material table.
layout(set=0,binding=16) uniform sampler2D SdfAtlas;
const int SP_SDF_MAX_STEPS=96;
const float SP_SDF_MAX_RANGE=128.0;
const int SP_SDF_MAX_REGIONS=8;
const int SP_SDF_MAX_NODES=2*SP_SDF_MAX_REGIONS-1;

uvec4 spSdfBytes(ivec2 texel) {
    return uvec4(round(texelFetch(SdfAtlas,texel,0)*255.0));
}

float spSdfMetadata(int column,int row) {
    uvec4 bytes=spSdfBytes(ivec2(column,row));
    return uintBitsToFloat(bytes.x|(bytes.y<<8u)|(bytes.z<<16u)|(bytes.w<<24u));
}

uvec4 spSdfVoxel(ivec3 index,int size,int firstRow) {
    index=clamp(index,ivec3(0),ivec3(size-1));
    return spSdfBytes(ivec2(index.x+index.y*size,firstRow+index.z));
}

float spSdfDistance(uvec4 voxel) {
    return (float(voxel.x|(voxel.y<<8u))-32768.0)/4096.0;
}

float spSdfInterpolated(vec3 p,int size,int firstRow) {
    vec3 grid=p-0.5;
    ivec3 base=ivec3(floor(grid));
    vec3 f=fract(grid);
    float d000=spSdfDistance(spSdfVoxel(base,size,firstRow));
    float d100=spSdfDistance(spSdfVoxel(base+ivec3(1,0,0),size,firstRow));
    float d010=spSdfDistance(spSdfVoxel(base+ivec3(0,1,0),size,firstRow));
    float d110=spSdfDistance(spSdfVoxel(base+ivec3(1,1,0),size,firstRow));
    float d001=spSdfDistance(spSdfVoxel(base+ivec3(0,0,1),size,firstRow));
    float d101=spSdfDistance(spSdfVoxel(base+ivec3(1,0,1),size,firstRow));
    float d011=spSdfDistance(spSdfVoxel(base+ivec3(0,1,1),size,firstRow));
    float d111=spSdfDistance(spSdfVoxel(base+ivec3(1,1,1),size,firstRow));
    return mix(mix(mix(d000,d100,f.x),mix(d010,d110,f.x),f.y),
        mix(mix(d001,d101,f.x),mix(d011,d111,f.x),f.y),f.z);
}

// Test parallel axes separately to avoid 0/0.
bool spSdfClip(vec3 origin,vec3 direction,vec3 minimum,vec3 maximum,inout float start,inout float end) {
    for(int axis=0;axis<3;++axis) {
        if(abs(direction[axis])<1e-7) {
            if(origin[axis]<minimum[axis]||origin[axis]>=maximum[axis])return false;
            continue;
        }
        float a=(minimum[axis]-origin[axis])/direction[axis];
        float b=(maximum[axis]-origin[axis])/direction[axis];
        start=max(start,min(a,b));
        end=min(end,max(a,b));
    }
    return start<end;
}

vec4 spSdfGrid(int metadataRow) {
    return vec4(spSdfMetadata(0,metadataRow),spSdfMetadata(1,metadataRow),spSdfMetadata(2,metadataRow),spSdfMetadata(3,metadataRow));
}

bool spSdfValidGrid(vec4 grid) {
    return grid.w>0.0&&!any(isnan(grid))&&!any(isinf(grid));
}

bool spSdfStep(vec3 p,int size,int firstRow,out float advance,out uint material) {
    uvec4 voxel=spSdfVoxel(ivec3(floor(p)),size,firstRow);
    material=voxel.z;
    // The point is within sqrt(3)/2 voxels of its sample. Include quantization error.
    float safeDistance=spSdfDistance(voxel)-0.867;
    advance=max(safeDistance,0.125);
    // Near the surface, interpolate to suppress staircase self-hits from nearest-voxel sampling.
    return safeDistance<=0.125&&spSdfInterpolated(p,size,firstRow)<=0.2;
}

struct SpSdfHit {
    vec3 LocalPosition;
    vec4 Grid;
    int Size;
    int FirstRow;
    uint Material;
};

bool spTraceSingleSdf(vec3 position,vec3 normal,vec3 direction,int size,float range,out SpSdfHit hit) {
    vec4 grid=spSdfGrid(size);
    if(!spSdfValidGrid(grid))return false;
    vec3 origin=(position-grid.xyz)/grid.w+0.65*normal;
    float start=0.0;
    float end=range/grid.w;
    if(!spSdfClip(origin,direction,vec3(0.0),vec3(size),start,end))return false;

    for(int step=0;step<SP_SDF_MAX_STEPS&&start<end;++step) {
        vec3 p=origin+direction*start;
        float advance;
        uint material;
        if(spSdfStep(p,size,0,advance,material)) {
            hit=SpSdfHit(p,grid,size,0,material);
            return true;
        }
        start+=advance;
    }
    return false;
}

struct SpSdfInterval {
    float Start;
    float End;
    int Region;
};

int spSdfFirstRow(int region,int size) {
    return 1+region*(size+1);
}

bool spTraceSdfBvh(vec3 position,vec3 normal,vec3 direction,int size,int count,float range,out SpSdfHit hit) {
    vec4 grids[SP_SDF_MAX_REGIONS];
    float minSpacing=3.402823e38;
    float receiverSpacing=minSpacing;
    for(int i=0;i<count;++i) {
        vec4 grid=spSdfGrid(spSdfFirstRow(i,size)+size);
        if(!spSdfValidGrid(grid))return false;
        grids[i]=grid;
        minSpacing=min(minSpacing,grid.w);
        vec3 local=(position-grid.xyz)/grid.w;
        if(all(greaterThanEqual(local,vec3(0.0)))&&all(lessThan(local,vec3(size))))receiverSpacing=min(receiverSpacing,grid.w);
    }

    // Use one biased world-space ray for every region so overlap distances are comparable.
    float biasSpacing=receiverSpacing<3.402823e38?receiverSpacing:minSpacing;
    vec3 origin=position+(0.65*biasSpacing)*normal;
    SpSdfInterval intervals[SP_SDF_MAX_REGIONS];
    int activeCount=0;
    int stack[SP_SDF_MAX_REGIONS];
    stack[0]=0;
    int pending=1;
    int nodeCount=2*count-1;
    for(int visited=0;visited<SP_SDF_MAX_NODES&&pending>0;++visited) {
        int node=stack[--pending];
        int column=1+7*node;
        vec3 minimum=vec3(spSdfMetadata(column,0),spSdfMetadata(column+1,0),spSdfMetadata(column+2,0));
        vec3 maximum=vec3(spSdfMetadata(column+3,0),spSdfMetadata(column+4,0),spSdfMetadata(column+5,0));
        float start=0.0,end=range;
        if(!spSdfClip(origin,direction,minimum,maximum,start,end))continue;
        ivec3 links=ivec3(spSdfBytes(ivec2(column+6,0)).xyz);
        if(links.z!=255) {
            if(links.z>=count||activeCount>=count)return false;
            intervals[activeCount++]=SpSdfInterval(start,end,links.z);
        } else {
            if(links.x<=node||links.y<=node||links.x>=nodeCount||links.y>=nodeCount||pending+2>SP_SDF_MAX_REGIONS)return false;
            stack[pending++]=links.y;
            stack[pending++]=links.x;
        }
    }

    // Interleave marches by their current world-space distance. Once a sample
    // hits, no other interval has a nearer sample pending. This also prevents
    // an overlapping empty region from consuming the entire march budget first.
    for(int step=0;step<SP_SDF_MAX_STEPS&&activeCount>0;++step) {
        int next=0;
        for(int i=1;i<activeCount;++i) {
            if(intervals[i].Start<intervals[next].Start
                ||(intervals[i].Start==intervals[next].Start&&intervals[i].Region<intervals[next].Region))next=i;
        }
        SpSdfInterval interval=intervals[next];
        vec4 grid=grids[interval.Region];
        int firstRow=spSdfFirstRow(interval.Region,size);
        vec3 p=(origin-grid.xyz)/grid.w+direction*(interval.Start/grid.w);
        float advance;
        uint material;
        if(spSdfStep(p,size,firstRow,advance,material)) {
            hit=SpSdfHit(p,grid,size,firstRow,material);
            return true;
        }
        interval.Start+=advance*grid.w;
        if(interval.Start<interval.End)intervals[next]=interval;
        else intervals[next]=intervals[--activeCount];
    }
    return false;
}

bool spTraceSdfHit(vec3 position,vec3 normal,vec3 direction,float maxRange,out SpSdfHit hit) {
    float range=min(maxRange,SP_SDF_MAX_RANGE);
    if(range<=0.0)return false;
    ivec2 atlas=textureSize(SdfAtlas,0);
    int size=atlas.y-1;
    if(size>=16&&size<=64&&atlas.x==size*size)return spTraceSingleSdf(position,normal,direction,size,range,hit);
    if(atlas.x<256||atlas.x>4096)return false;
    size=int(sqrt(float(atlas.x)));
    uvec4 header=spSdfBytes(ivec2(0));
    int count=int(header.w);
    if(atlas.x!=size*size||any(notEqual(header.xyz,uvec3(83,68,70)))||count<1||count>SP_SDF_MAX_REGIONS
        ||atlas.y!=1+count*(size+1))return false;
    return spTraceSdfBvh(position,normal,direction,size,count,range,hit);
}

#ifndef SP_SDF_GEOMETRY_ONLY
void spSdfMaterial(SpSdfHit hit,out vec3 albedo,out vec3 emissive) {
    int material=4+6*int(min(hit.Material,31u));
    int row=hit.FirstRow+hit.Size;
    albedo=vec3(spSdfMetadata(material,row),spSdfMetadata(material+1,row),spSdfMetadata(material+2,row));
    emissive=vec3(spSdfMetadata(material+3,row),spSdfMetadata(material+4,row),spSdfMetadata(material+5,row));
}

vec3 spSdfGradient(SpSdfHit hit) {
    const vec2 offset=vec2(0.5,0.0);
    vec3 p=hit.LocalPosition;
    int size=hit.Size,firstRow=hit.FirstRow;
    return vec3(spSdfInterpolated(p+offset.xyy,size,firstRow)-spSdfInterpolated(p-offset.xyy,size,firstRow),
        spSdfInterpolated(p+offset.yxy,size,firstRow)-spSdfInterpolated(p-offset.yxy,size,firstRow),
        spSdfInterpolated(p+offset.yyx,size,firstRow)-spSdfInterpolated(p-offset.yyx,size,firstRow));
}

vec3 spSdfAmbient(vec3 albedo,vec3 normal) {
    // Low-cost diffuse sky approximation for the static surface.
    return spChainEnabled(SP_CHAIN_ENVIRONMENT)?albedo*textureLod(Environment,normal,0.0).rgb:vec3(0.0);
}

bool spTraceSdf(vec3 position,vec3 normal,vec3 direction,out vec3 radiance) {
    radiance=vec3(0.0);
    SpSdfHit hit;
    if(!spTraceSdfHit(position,normal,direction,engine_Frame.CameraParams.y,hit))return false;
    vec3 albedo,emissive;
    spSdfMaterial(hit,albedo,emissive);
    if(spChainEnabled(SP_CHAIN_ENVIRONMENT)) {
        vec3 gradient=spSdfGradient(hit);
        vec3 hitNormal=dot(gradient,gradient)>1e-6?normalize(gradient):-direction;
        emissive+=spSdfAmbient(albedo,hitNormal);
    }
    radiance=clamp(emissive,vec3(0.0),vec3(65504.0));
    return true;
}
#endif
