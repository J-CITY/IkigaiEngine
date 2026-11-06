vec3 reflectedDirection(vec3 n,vec3 v,float rough) {
    // Sample the GGX half vector in the receiver's local frame.
    float alpha=rough*rough,u=randomFloat(),phi=6.2831853*randomFloat();
    float cosTheta=sqrt((1-u)/max(1+(alpha*alpha-1)*u,.00001));
    float sinTheta=sqrt(max(0,1-cosTheta*cosTheta));
    vec3 t=normalize(cross(abs(n.y)<.99?vec3(0,1,0):vec3(1,0,0),n));
    vec3 h=normalize(t*(sinTheta*cos(phi))+cross(n,t)*(sinTheta*sin(phi))+n*cosTheta);
    return reflect(-v,h);
}

vec3 weightReflectionRadiance(vec3 radiance,vec3 n,vec3 v,vec3 direction,float nl,float rough,vec3 f0) {
    // BRDF / GGX-direction PDF, including the receiver's Fresnel and geometry terms.
    vec3 h=normalize(v+direction);
    float nv=max(dot(n,v),.0001),nh=max(dot(n,h),.0001),vh=max(dot(v,h),0);
    float k=(rough+1)*(rough+1)/8;
    float g=nv/(nv*(1-k)+k)*nl/(nl*(1-k)+k);
    vec3 fresnel=f0+(1-f0)*pow(1-vh,5);
    return radiance*fresnel*g*vh/max(nv*nh,.0001);
}
