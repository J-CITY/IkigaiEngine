// Approximation for light passing through thin leaves and blades. The G-buffer
// roughness alpha stores 1-transmission, retaining 1 for existing opaque surfaces.
vec3 thinSurfaceLight(vec3 color, float metallic, float transmission, vec3 n, vec3 v, vec3 l) {
    float back = max(dot(-n, l), 0.0);
    float forward = pow(max(dot(-v, l), 0.0), 4.0);
    return color * (1.0-metallic) * transmission * back * (0.45+0.55*forward) / 3.14159265;
}
