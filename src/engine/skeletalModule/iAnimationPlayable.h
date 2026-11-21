#pragma once

#include "skeleton.h"
#include <memory>

namespace IKIGAI::SKELETON {
class IAnimationPlayable {
public:
	virtual ~IAnimationPlayable() = default;
	virtual void update(float dt) = 0;
	virtual Pose* getPose() = 0;
	virtual std::unique_ptr<IAnimationPlayable> clone() const = 0;
};
}
