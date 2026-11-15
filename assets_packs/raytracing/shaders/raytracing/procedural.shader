{
  "Shaders": [
    {"Name":"primary","Stage":"RAYGEN","Path":"/shaders/raytracing/procedural.rgen"},
    {"Name":"miss","Stage":"MISS","Path":"/shaders/raytracing/procedural.rmiss"},
    {"Name":"hit","Stage":"CLOSEST_HIT","Path":"/shaders/raytracing/procedural.rchit"},
    {"Name":"alpha","Stage":"ANY_HIT","Path":"/shaders/raytracing/procedural.rahit"},
    {"Name":"intersection","Stage":"INTERSECTION","Path":"/shaders/raytracing/procedural.rint"}
  ],
  "Groups": [
    {"Name":"primary","General":"primary"},
    {"Name":"miss","General":"miss"},
    {"Name":"surface","ClosestHit":"hit","AnyHit":"alpha","Intersection":"intersection"}
  ],
  "MaxPayloadSize":32,"MaxAttributeSize":8
}
