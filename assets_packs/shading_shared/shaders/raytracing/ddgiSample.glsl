vec2 octEncode(vec3 n){n/=abs(n.x)+abs(n.y)+abs(n.z);vec2 s=vec2(n.x>=0?1:-1,n.y>=0?1:-1);return (n.z>=0?n.xy:(1-abs(n.yx))*s)*.5+.5;}
vec3 probeIrradiance(vec3 p,vec3 n,vec4 originSpacing,ivec3 counts,sampler2D irradiance,sampler2D distanceAtlas,bool scrolling,out float confidence) {
    vec3 grid=(p-originSpacing.xyz)/originSpacing.w;
    if(any(lessThan(grid,vec3(0)))||any(greaterThan(grid,vec3(counts-1)))){confidence=0;return vec3(0);}
    ivec3 base=clamp(ivec3(floor(grid)),ivec3(0),counts-2);vec3 fraction=clamp(grid-vec3(base),0,1);
    vec3 sum=vec3(0);float total=0,valid=0,coverage=0;
    vec2 atlasSize=vec2(textureSize(irradiance,0));
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        ivec3 index=base+ivec3(x,y,z);vec3 probe=originSpacing.xyz+vec3(index)*originSpacing.w;
        vec3 delta=p+n*.03-probe;float d=length(delta);vec3 direction=delta/max(d,.0001);
        vec2 tile=vec2(index.x,index.y+index.z*counts.y)*10;
        vec2 normalUV=(tile+vec2(1)+octEncode(n)*8)/atlasSize;
        vec2 directionUV=(tile+vec2(1)+octEncode(direction)*8)/atlasSize;
        vec3 bary=mix(1-fraction,fraction,vec3(x,y,z));float weight=bary.x*bary.y*bary.z;
        vec4 value=textureLod(irradiance,normalUV,0);
        if(scrolling) {
            // Unavailable cells have no radiance or moments. Their interpolation
            // weight goes to the caller's fallback, independently of visibility.
            if(value.a<=0)continue;
            coverage+=weight*value.a;
        }
        vec2 moments=textureLod(distanceAtlas,directionUV,0).rg;
        float variance=max(moments.y-moments.x*moments.x,.0001);
        float visibility=d<=moments.x+.05?1:pow(variance/(variance+pow(d-moments.x,2)),3);
        weight*=max(.05,dot(n,-direction))*max(.001,visibility);
        sum+=value.rgb*weight;valid+=value.a*weight;total+=weight;
    }
    confidence=total>1e-6?clamp(scrolling?coverage:valid/total,0,1):0;return total>1e-6?sum/total:vec3(0);
}
vec3 probeIrradiance(vec3 p,vec3 n,vec4 originSpacing,ivec3 counts,sampler2D irradiance,sampler2D distanceAtlas,out float confidence) {
    return probeIrradiance(p,n,originSpacing,counts,irradiance,distanceAtlas,false,confidence);
}
