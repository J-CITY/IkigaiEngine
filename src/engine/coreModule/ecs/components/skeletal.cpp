#include "skeletal.h"
#include "../componentManager.h"
#include "../object.h"
#include "resourceModule/resource/bone.h"
#include "utilsModule/log/loggerDefine.h"
#include <string>

using namespace IKIGAI;
using namespace IKIGAI::ECS;

Skeletal::Skeletal(UTILS::Ref<ECS::Object> obj) : ComponentBase(obj) {
  __NAME__ = "Skeletal";
}

Skeletal::Skeletal(UTILS::Ref<ECS::Object> _obj, const std::string &_path,
                   const std::optional<std::string> &_startAnimation)
    : ComponentBase(_obj), animationPath(_path), curAnimation(_startAnimation) {
  __NAME__ = "Skeletal";

  auto &world = RESOURCES::ServiceManager::Get<ECS2::World>();
  auto componentManager = world.getComponentManager();
  auto model = componentManager->getComponent<ModelRenderer>(obj->getID());
  // auto model =
  // ECS::ComponentManager::GetInstance().getComponent<ModelRenderer>(obj->getID());
  if (!model || !model->getModel()) {
    LOG_ERROR << "Skeletal: model is not loaded: " << animationPath;
    return;
  }
  animations = RESOURCES::Animation::LoadAnimations(animationPath,
                                                    model->getModel().get());

  if (_startAnimation) {
    setAnimation(_startAnimation.value());
  }
}

Skeletal::Skeletal(UTILS::Ref<ECS::Object> _obj, const Descriptor &_descriptor)
    : ComponentBase(_obj), animationPath(_descriptor.Path),
      curAnimation(_descriptor.Animation) {
  __NAME__ = "Skeletal";
  auto &world = RESOURCES::ServiceManager::Get<ECS2::World>();
  auto componentManager = world.getComponentManager();
  auto model = componentManager->getComponent<ModelRenderer>(obj->getID());
  // auto model =
  // ECS::ComponentManager::GetInstance().getComponent<ModelRenderer>(obj->getID());
  if (!model || !model->getModel()) {
    LOG_ERROR << "Skeletal: model is not loaded: " << animationPath;
    return;
  }
  animations = RESOURCES::Animation::LoadAnimations(animationPath,
                                                    model->getModel().get());

  if (curAnimation) {
    setAnimation(curAnimation.value());
  }
}

void Skeletal::onUpdate(std::chrono::duration<double> dt) {
  if (!animator) return;
  // TODO: refactor it
  if (animator->blender) animator->blender->point = {pointX, pointY};
  animator->UpdateAnimation(static_cast<float>(dt.count()));
}

void Skeletal::setAnimation(std::string id) {
  if (!animations.contains(id)) {
    LOG_ERROR
        << ("Skeletal::setAnimation: Can not set animation (key do not exist)");
    return;
  }
  curAnimation = id;
  // if (!id.empty()) {
  animator =
      std::make_unique<IKIGAI::RESOURCES::Animator>(animations.at(id).get());
  //}
}

void Skeletal::setAnimationPath(std::string id) {
  animator.reset();
  animations.clear();
  animationPath = id;
  auto &world = RESOURCES::ServiceManager::Get<ECS2::World>();
  auto componentManager = world.getComponentManager();
  auto model = componentManager->getComponent<ModelRenderer>(obj->getID());
  // auto model =
  // ECS::ComponentManager::GetInstance().getComponent<ModelRenderer>(obj->getID());
  if (!model || !model->getModel()) {
    LOG_ERROR << "Skeletal: model is not loaded: " << animationPath;
    return;
  }
  animations = RESOURCES::Animation::LoadAnimations(animationPath,
                                                    model->getModel().get());
  if (curAnimation && !curAnimation->empty()) setAnimation(*curAnimation);
}

std::string Skeletal::getAnimationPath() const { return animationPath; }

std::string Skeletal::getCurrentAnimationName() const {
  if (!curAnimation) {
    return "";
  }
  return curAnimation.value();
}

Skeletal::Descriptor Skeletal::getDescriptor() const {
  Descriptor descriptor;
  descriptor.Type = GetType<Skeletal>();
  descriptor.Path = getAnimationPath();
  descriptor.Animation = getCurrentAnimationName();
  return descriptor;
}
// #include <rttr/registration>
//
// RTTR_REGISTRATION
//{
//	rttr::registration::class_<IKIGAI::ECS::Skeletal>("Skeletal")
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	)
//	.property("AnimationPath", &IKIGAI::ECS::Skeletal::getAnimationPath,
//&IKIGAI::ECS::Skeletal::setAnimationPath)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE |
//MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET,
//EditorMetaInfo::WidgetType::STRING)
//	)
//	.property("Animation", &IKIGAI::ECS::Skeletal::getCurrentAnimationName,
//&IKIGAI::ECS::Skeletal::setAnimation)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE |
//MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR | MetaInfo::OPTIONAL_PARAM),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET,
//EditorMetaInfo::WidgetType::STRING)
//	);
// }
