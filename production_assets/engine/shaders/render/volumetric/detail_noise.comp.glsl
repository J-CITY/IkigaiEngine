#version 450
// Ported from F:/projects/_/assets_save/gl/Volumetric/detail_noise_cs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    int u_Size;
} data_UBO;

#define UI0 1597334673U
#define UI1 3812015801U
#define UI2 uvec2(UI0, UI1)
#define UI3 uvec3(UI0, UI1, 2798796415U)
#define UIF (1.0 / float(0xffffffffU))

vec3 hash_33(vec3 p)
{
	uvec3 q = uvec3(ivec3(p)) * UI3;
	q = (q.x ^ q.y ^ q.z)*UI3;
	return -1. + 2. * vec3(q) * UIF;
}

float remap(float x, float a, float b, float c, float d)
{
    return (((x - a) / (b - a)) * (d - c)) + c;
}

float gradient_noise(vec3 x, float freq)
{
    
    vec3 p = floor(x);
    vec3 w = fract(x);

    vec3 u = w * w * w * (w * (w * 6. - 15.) + 10.);

    vec3 ga = hash_33(mod(p + vec3(0., 0., 0.), freq));
    vec3 gb = hash_33(mod(p + vec3(1., 0., 0.), freq));
    vec3 gc = hash_33(mod(p + vec3(0., 1., 0.), freq));
    vec3 gd = hash_33(mod(p + vec3(1., 1., 0.), freq));
    vec3 ge = hash_33(mod(p + vec3(0., 0., 1.), freq));
    vec3 gf = hash_33(mod(p + vec3(1., 0., 1.), freq));
    vec3 gg = hash_33(mod(p + vec3(0., 1., 1.), freq));
    vec3 gh = hash_33(mod(p + vec3(1., 1., 1.), freq));

    float va = dot(ga, w - vec3(0., 0., 0.));
    float vb = dot(gb, w - vec3(1., 0., 0.));
    float vc = dot(gc, w - vec3(0., 1., 0.));
    float vd = dot(gd, w - vec3(1., 1., 0.));
    float ve = dot(ge, w - vec3(0., 0., 1.));
    float vf = dot(gf, w - vec3(1., 0., 1.));
    float vg = dot(gg, w - vec3(0., 1., 1.));
    float vh = dot(gh, w - vec3(1., 1., 1.));

    return va + 
           u.x * (vb - va) + 
           u.y * (vc - va) + 
           u.z * (ve - va) + 
           u.x * u.y * (va - vb - vc + vd) + 
           u.y * u.z * (va - vc - ve + vg) + 
           u.z * u.x * (va - vb - ve + vf) + 
           u.x * u.y * u.z * (-va + vb + vc - vd + ve - vf - vg + vh);
}

float worley_noise(vec3 uv, float freq)
{    
    vec3 id = floor(uv);
    vec3 p = fract(uv);
    
    float min_dist = 10000.;
    for (float x = -1.; x <= 1.; ++x)
    {
        for(float y = -1.; y <= 1.; ++y)
        {
            for(float z = -1.; z <= 1.; ++z)
            {
                vec3 offset = vec3(x, y, z);
            	vec3 h = hash_33(mod(id + offset, vec3(freq))) * .5 + .5;
    			h += offset;
            	vec3 d = p - h;
           		min_dist = min(min_dist, dot(d, d));
            }
        }
    }

    return 1. - min_dist;
}

float perlin_fbm(vec3 p, float freq, int octaves)
{
    float G = exp2(-.85);
    float amp = 1.;
    float noise = 0.;
    for (int i = 0; i < octaves; ++i)
    {
        noise += amp * gradient_noise(p * freq, freq);
        freq *= 2.;
        amp *= G;
    }
    
    return noise;
}

float worley_fbm(vec3 p, float freq)
{
    return worley_noise(p*freq, freq) * .625 +
           worley_noise(p*freq*2., freq*2.) * .25 +
           worley_noise(p*freq*4., freq*4.) * .125;
}

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(set=0,binding=8,rgba16f) uniform image3D i_Noise;

void main()
{
    vec3 tex_coord = (vec3(gl_GlobalInvocationID) + vec3(0.5f)) / float(data_UBO.u_Size);

    float freq = 8.0f;

    float worley0 = worley_fbm(tex_coord, freq);
    float worley1 = worley_fbm(tex_coord, freq * 2.0f);
    float worley2 = worley_fbm(tex_coord, freq * 4.0f);
 
    vec4 worley = vec4(worley0, worley1, worley2, 0.0f); 
    
    imageStore(i_Noise, ivec3(gl_GlobalInvocationID), worley);
}

