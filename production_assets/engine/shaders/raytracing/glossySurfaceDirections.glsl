const int GlossyAzimuthCount=8;
const int GlossyElevationCount=4;
const float GlossyMinViewCosine=0.25;
const float GlossyAzimuthStep=6.28318530718/float(GlossyAzimuthCount);
const float GlossyElevationStep=(1.0-GlossyMinViewCosine)/float(GlossyElevationCount-1);

float glossyViewCosine(int elevation) {
    return GlossyMinViewCosine+float(elevation)*GlossyElevationStep;
}

ivec2 glossySurfaceTile(int card,int azimuth,int elevation) {
    return ivec2(azimuth,card*GlossyElevationCount+elevation)*SP_SURFACE_TILE;
}
