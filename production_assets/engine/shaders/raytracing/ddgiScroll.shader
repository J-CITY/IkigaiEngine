{
  "Shaders": [
    {
      "Name": "primary",
      "Stage": "RAYGEN",
      "Path": "/shaders/raytracing/ddgiScroll.rgen"
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
      "Name": "visibility",
      "Stage": "MISS",
      "Path": "/shaders/raytracing/visibility.rmiss"
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
      "Name": "shadowMiss",
      "General": "visibility"
    },
    {
      "Name": "surface",
      "ClosestHit": "surface",
      "AnyHit": "alpha"
    }
  ],
  "MaxRecursionDepth": 1,
  "PushConstantSize": 48,
  "RequiresDescriptorIndexing": true
}
