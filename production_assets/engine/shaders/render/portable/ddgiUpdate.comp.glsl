#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=8) uniform sampler2D RayRadiance;
layout(set=0,binding=9) uniform sampler2D RayDirectionDistance;
layout(set=0,binding=10) uniform sampler2D PreviousIrradiance;
layout(set=0,binding=11) uniform sampler2D PreviousDistance;
layout(set=0,binding=12,rgba32f) uniform image2D Irradiance;
layout(set=0,binding=13,rg32f) uniform image2D Distance;
layout(std140,set=0,binding=6) uniform RayFrame {vec4 State;} engine_RayFrame;
layout(std140,set=0,binding=4) uniform Parameters {vec4 Counts;float Hysteresis;} data_UBO;
vec3 octDecode(vec2 uv) {vec2 f=uv*2-1;vec3 n=vec3(f,1-abs(f.x)-abs(f.y));if(n.z<0)n.xy=(1-abs(n.yx))*sign(n.xy);return normalize(n);}
void main() {
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy),size=imageSize(Irradiance);if(any(greaterThanEqual(pixel,size)))return;
    ivec2 tile=pixel/10,local=pixel%10;
    // Copy the mirrored octahedral edge by evaluating its matching interior
    // direction directly; no cross-workgroup border-read dependency is needed.
    if(local.x==0){local.x=1;local.y=9-local.y;}else if(local.x==9){local.x=8;local.y=9-local.y;}
    if(local.y==0){local.y=1;local.x=9-local.x;}else if(local.y==9){local.y=8;local.x=9-local.x;}
    vec3 normal=octDecode((vec2(local)-.5)/8);
    int probe=tile.x+(tile.y%int(data_UBO.Counts.y))*int(data_UBO.Counts.x)+(tile.y/int(data_UBO.Counts.y))*int(data_UBO.Counts.x*data_UBO.Counts.y);
    ivec2 raySize=textureSize(RayRadiance,0);int rays=min(int(data_UBO.Counts.w),raySize.x);int row=min(probe,raySize.y-1);
    vec3 irradiance=vec3(0);vec2 moments=vec2(0);float distanceWeight=0,valid=0;
    for(int r=0;r<rays;++r) {
        vec4 ray=texelFetch(RayDirectionDistance,ivec2(r,row),0),radiance=texelFetch(RayRadiance,ivec2(r,row),0);
        float cosine=max(dot(normal,ray.xyz),0),weight=pow(cosine,16);
        irradiance+=radiance.rgb*cosine;moments+=vec2(ray.w,ray.w*ray.w)*weight;distanceWeight+=weight;valid+=radiance.a;
    }
    vec4 next=vec4(irradiance*4/max(float(rays),1),valid/max(float(rays),1));
    moments=distanceWeight>1e-7?moments/distanceWeight:vec2(100,10000);
    if(engine_RayFrame.State.w>.5) {
        float h=clamp(data_UBO.Hysteresis,0,.98);next=mix(next,texelFetch(PreviousIrradiance,pixel,0),h);moments=mix(moments,texelFetch(PreviousDistance,pixel,0).rg,h);
    }
    imageStore(Irradiance,pixel,next);imageStore(Distance,pixel,vec4(moments,0,1));
}
