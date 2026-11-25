#pragma once

#ifdef USE_EDITOR
namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}
namespace IKIGAI::EDITOR {
inline constexpr char ScreenProbeRoomPath[] = "/scenes/screen-probe-room.json";
inline constexpr char VirtualGeometryGalleryPath[] = "/scenes/virtual-geometry-gallery.json";
inline constexpr char FurRabbitPath[] = "/scenes/fur-rabbit.json";
inline constexpr char Cloth2DPath[] = "/scenes/physics2d-cloth-wind.json";
inline constexpr char Cloth3DPath[] = "/scenes/physics3d-cloth.json";
inline constexpr char SoftBodyPath[] = "/scenes/physics3d-soft.json";
inline constexpr char ParticleEventPath[] = "/scenes/particle-events-demo.json";

class ScreenProbeRoomControls {
public:
	void reset();
	void draw(SCENE_SYSTEM::Scene& scene);

private:
	bool mIndirect = true;
};

void DrawVirtualGeometryGallery(SCENE_SYSTEM::Scene& scene);
void DrawFurRabbit(SCENE_SYSTEM::Scene& scene);
void DrawClothWind(SCENE_SYSTEM::Scene& scene, bool planar);
void DrawSoftBodyDemo();
void DrawParticleEvents(SCENE_SYSTEM::Scene& scene);
}
#endif
