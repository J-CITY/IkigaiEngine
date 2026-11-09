#include "interface/driverInterface.h"
#include <iostream>

namespace IKIGAI::RENDER {
void DriverInterface::prepareFrameGeneration(bool pipelineEnabled, MATH::Vector2i render, MATH::Vector2i display) {
	mPresentationUIRendered = false;
	auto requested = settings.frameGeneration;
	if (requested.multiplier < 2 || requested.multiplier > 6)
		throw std::runtime_error("Frame generation multiplier must be in [2,6]");
	if (!pipelineEnabled)
		requested.technology = FrameGenerationType::OFF;
	const bool changed = !mFrameGenerationSettingsLoaded || requested != mFrameGenerationSettings;
	if (changed) {
		mFrameGeneration.reset();
		mFrameGenerationFailed = false;
		mFrameGenerationStatus = { };
	}
	mFrameGenerationSettingsLoaded = true;
	mFrameGenerationSettings = requested;
	if (requested.technology == FrameGenerationType::OFF || display.x <= 0 || display.y <= 0) {
		if (mFrameGeneration)
			mFrameGeneration->suspend();
		if (settings.frameGeneration.technology != FrameGenerationType::OFF && !pipelineEnabled)
			mFrameGenerationStatus.Reason = "Pipeline has no enabled FRAME_GENERATION stage";
		return;
	}
	if (mFrameGenerationFailed) {
		mFrameGeneration.reset();
		return;
	}
	try {
		if (!mFrameGeneration) {
			if (requested.technology == FrameGenerationType::AUTO) {
				try {
					mFrameGeneration = createFrameGeneration(FrameGenerationType::DLSS);
					mFrameGeneration->configure(requested, render, display);
				} catch (const UnsupportedFrameGeneration&) {
					mFrameGeneration.reset();
					mFrameGeneration = createFrameGeneration(FrameGenerationType::FSR);
				}
			}
			else
				mFrameGeneration = createFrameGeneration(requested.technology);
		}
		mFrameGeneration->configure(requested, render, display);
		mFrameGenerationStatus = mFrameGeneration->status();
	} catch (const UnsupportedFrameGeneration& error) {
		mFrameGeneration.reset();
		mFrameGenerationFailed = true;
		mFrameGenerationStatus = { };
		mFrameGenerationStatus.Reason = error.what();
		std::cerr << "FRAMEGEN_FALLBACK " << error.what() << '\n';
	}
}
void DriverInterface::dispatchFrameGeneration(const FrameGenerationInputs& inputs) {
	if (!mFrameGeneration || mFrameGenerationFailed)
		return;
	try {
		ValidateFrameGenerationInputs(inputs);
		mFrameGeneration->prepare(stageFrameGenerationInputs(inputs));
		mFrameGenerationStatus = mFrameGeneration->status();
	} catch (const UnsupportedFrameGeneration& error) {
		mFrameGeneration->suspend();
		mFrameGenerationFailed = true;
		mFrameGenerationStatus.Active = false;
		mFrameGenerationStatus.Reason = error.what();
		std::cerr << "FRAMEGEN_FALLBACK " << error.what() << '\n';
	}
}
}
