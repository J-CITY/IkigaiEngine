{
  "Shaders": [
    {
      "Name": "primary",
      "Stage": "RAYGEN",
      "Path": "/shaders/raytracing/skinningCheck.rgen"
    },
    {
      "Name": "miss",
      "Stage": "MISS",
      "Path": "/shaders/raytracing/surface.rmiss"
    },
    {
      "Name": "surface",
      "Stage": "CLOSEST_HIT",
      "Path": "/shaders/raytracing/surface.rchit"
    },
    {
      "Name": "alpha",
      "Stage": "ANY_HIT",
      "Path": "/shaders/raytracing/alpha.rahit"
    }
  ],
  "Groups": [
    {
      "Name": "primary",
      "General": "primary"
    },
    {
      "Name": "miss",
      "General": "miss"
    },
    {
      "Name": "surface",
      "ClosestHit": "surface",
      "AnyHit": "alpha"
    }
  ],
  "MaxRecursionDepth": 1,
  "PushConstantSize": 0,
  "RequiresDescriptorIndexing": true
}
