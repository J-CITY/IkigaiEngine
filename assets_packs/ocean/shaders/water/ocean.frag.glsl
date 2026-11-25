#version 450
#extension GL_GOOGLE_include_directive : require
#include "waves.glsl"

layout(set=0,binding=8) uniform sampler2D u_FoamMask;
layout(location=0) in vec3 World;
layout(location=1) in vec4 PreviousClip;
layout(location=0) out vec4 VelocityOut;
layout(location=1) out vec4 NormalOut;
layout(location=2) out vec4 AlbedoOut;
layout(location=3) out vec4 RoughOut;

// Each packet has a different heading and a curved front. Filter by the
// projected phase change so narrow highlights do not crawl across distant waves.
void addPacket(vec2 p, vec2 direction, float k, float strength, float phase,
               inout vec2 slope, inout float unresolved) {
    vec2 across = vec2(-direction.y, direction.x);
    float bend = dot(p, across) * k * 0.23 + phase;
    float carrier = k * dot(p, direction) + 1.7 * sin(bend) + 3.0 * phase;
    float envelope = 0.65 + 0.35 * sin(dot(p, direction + across) * k * 0.17 - phase);
    float weight = 1.0 - smoothstep(0.55, 2.8, fwidth(carrier));
    float amplitude = strength * envelope;
    slope += amplitude * weight * (direction + 0.391 * cos(bend) * across)
           * cos(carrier + 0.35 * sin(carrier));
    unresolved += amplitude * amplitude * (1.0 - weight * weight);
}

// Undo a little of the swell's orbital motion so the froth follows the water.
vec2 foamCoordinates(vec2 point) {
    vec2 displacement = vec2(0);
    vec4 waves[3] = vec4[3](water_.WaveA, water_.WaveB, water_.WaveC);
    for (int i = 0; i < 3; ++i) {
        displacement += normalize(waves[i].xy) * waves[i].z * cos(dot(point, waves[i].xy) + waves[i].w);
    }
    return point + 0.58 * displacement;
}

// Distance to the tapered waterline, in metres. Project the water-relative
// velocity onto the same ellipse to leave a widening streak downstream.
float hullFroth(vec2 point, float patchiness, float pixelWidth) {
    vec2 radius = water_.Hull.zw;
    if (water_.Weather.z <= 0.0 || min(radius.x, radius.y) <= 0.0) return 0.0;
    vec2 delta = point - water_.Hull.xy;
    vec2 relative = water_.HullMotion.zw;
    float speed = length(relative);
    float trailLength = min(5.0, speed * 4.0);
    if (length(delta) > max(radius.x, radius.y) + trailLength + 0.65) return 0.0;

    vec2 forward = water_.HullMotion.xy;
    vec2 side = vec2(forward.y, -forward.x);
    vec2 local = vec2(dot(delta, side), dot(delta, forward));
    vec2 q = local / radius;
    float waterline = (length(q) - 1.0) / max(length(q / radius) / max(length(q), 0.001), 0.001);
    float agitation = water_.Weather.w;
    float coarse = patchiness;
    float width = .09 + .21 * agitation;
    float contact = 1.0 - smoothstep(width * .18, width + pixelWidth, abs(waterline - .035 - .05 * coarse));

    vec2 downstream = -relative / max(speed, .001);
    vec2 lateral = vec2(-downstream.y, downstream.x);
    vec2 downstreamLocal = vec2(dot(downstream, side), dot(downstream, forward)) / radius;
    float hullEnd = inversesqrt(max(.001, dot(downstreamLocal, downstreamLocal)));
    float along = dot(delta, downstream) - hullEnd;
    float t = clamp(along / max(trailLength, .001), 0.0, 1.0);
    float spread = .18 + .4 * t;
    float crossTrail = abs(dot(delta, lateral) + .16 * coarse);
    float wake = smoothstep(.01, .22, speed) * smoothstep(-.18, .08, along) * (1.0 - smoothstep(.1, 1.0, t))
               * (1.0 - smoothstep(spread * .2, spread + pixelWidth, crossTrail));
    return water_.Weather.z * max(contact * (.26 + .74 * agitation), wake);
}

void main() {
    vec3 swell = surface(World.xz);
    vec2 wind = normalize(water_.WaveA.xy);
    vec2 across = vec2(-wind.y, wind.x);
    vec2 p = vec2(dot(World.xz, wind), dot(World.xz, across));
    float phase = water_.WaveA.w;
    float windStrength = 1.0 - exp(-0.10 * water_.Weather.x);
    vec2 detail = vec2(0);
    float variance = 0.0;
    addPacket(p, vec2(.966, .259), 3.7, .17, phase + .7, detail, variance);
    addPacket(p, vec2(.819, -.574), 7.3, .13, water_.WaveB.w + 1.9, detail, variance);
    addPacket(p, vec2(.454, .891), 14.9, .085, water_.WaveC.w - .4, detail, variance);
    addPacket(p, vec2(.985, -.174), 31.1, .065, 2.0 * phase + 2.4, detail, variance);
    addPacket(p, vec2(.602, -.799), 61.3, .045, 3.0 * water_.WaveB.w, detail, variance);
    vec2 slope = swell.yz + windStrength * (wind * detail.x + across * detail.y);
    vec3 normal = normalize(vec3(-slope.x, 1, -slope.y));

    float heightScale = max(.001, water_.WaveA.z + water_.WaveB.z + water_.WaveC.z);
    float crest = clamp(swell.x / heightScale, -1., 1.);
    float steepness = length(water_.WaveA.xy) * water_.WaveA.z;
    float breakage = smoothstep(.035, .28, steepness);
    vec2 film = foamCoordinates(World.xz);
    vec2 uv = vec2(dot(film, wind), dot(film, across)) / vec2(2.8, 2.1);
    const mat2 turn = mat2(.854, .521, -.521, .854);
    // Blend unrelated sample locations gradually to disguise the tile repeat.
    float region = smoothstep(-.6, .6, sin(dot(film, vec2(.71,.32))) + .65 * sin(dot(film, vec2(-.23,.81))));
    float first = texture(u_FoamMask, uv).r;
    float second = texture(u_FoamMask, turn * uv * .79 + vec2(.37,.61)).r;
    float filmDensity = mix(first, second, region);
    float smallFlakes = texture(u_FoamMask, turn * uv * 2.7 + vec2(.64,.28)).r;
    float density = clamp(filmDensity * .91 + smallFlakes * .23, 0., 1.);

    float waveNumber = max(length(water_.WaveA.xy), .001);
    float passedCrest = surface(World.xz + wind * (.95 / waveNumber)).x / heightScale;
    float breaking = smoothstep(.16, .85, crest);
    float receding = smoothstep(.18, .82, passedCrest) * smoothstep(-.08, .41, passedCrest - crest);
    float waveAir = water_.Weather.y * breakage * max(breaking, .65 * receding);
    float hullAir = hullFroth(World.xz, smallFlakes * 2.0 - .65, length(fwidth(World.xz)));
    float air = max(waveAir, hullAir);
    // Lower aeration exposes only isolated white knots; dense breaking crests
    // also reveal the fine connecting film captured in the mask.
    float coverage = smoothstep(mix(.54, .045, air), mix(.85, .61, air), density);
    float foam = sqrt(air) * coverage;

    vec3 waterColor = pow(water_.Tint.rgb, vec3(2.2));
    float crestLight = smoothstep(-.2, .94, crest) * smoothstep(.015, .6, heightScale);
    vec3 bodyColor = waterColor * mix(vec3(.52, .65, .81), vec3(.89, 1.45, 1.24), crestLight);
    // Wet edges scatter less light than densely packed bubbles.
    vec3 foamColor = mix(vec3(.31, .46, .46), vec3(.93, .96, .93), smoothstep(.16, .73, density));
    float roughness = sqrt(water_.Tint.w * water_.Tint.w + windStrength * windStrength * variance);

    // The deferred lighting pass evaluates Fresnel and reflects the scene sky
    // using these normals; crest transmission also uses the scene's actual sun.
    NormalOut = vec4(normal, 1);
    AlbedoOut = vec4(mix(bodyColor, foamColor, foam), 0);
    RoughOut = vec4(mix(roughness, .80, foam), 1, -(engine_UBO.View * vec4(World, 1)).z,
                    1.0 - .32 * crestLight * (1.0 - foam));
    vec2 previousUV = PreviousClip.xy / max(abs(PreviousClip.w), 1e-5) * sign(PreviousClip.w) * .5 + .5;
    previousUV.y = mix(previousUV.y, 1.0 - previousUV.y, engine_Frame.CameraParams.w);
    vec2 motion = gl_FragCoord.xy / engine_UBO.ViewportSize - previousUV - engine_Frame.FrameState.zw;
    VelocityOut = vec4(engine_Frame.FrameState.x > .5 ? motion : vec2(0), 0, 1);
}
