#include "facialAnimationComponent.h"

#include "coreModule/ecs/object.h"
#include "modelRenderer.h"
#include "renderModule/backends/interface/meshInterface.h"
#include "renderModule/backends/interface/modelInterface.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace IKIGAI::ECS {
FacialAnimationComponent::FacialAnimationComponent(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "FacialAnimationComponent";
	mDescriptor.Type = GetType<FacialAnimationComponent>();
}
FacialAnimationComponent::FacialAnimationComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
	: FacialAnimationComponent(object) {
	mDescriptor = static_cast<const Descriptor&>(descriptor);
}
FacialAnimationComponent::Descriptor FacialAnimationComponent::getDescriptor() const {
	return mDescriptor;
}
void FacialAnimationComponent::bind(const RENDER::ModelInterface& model) {
	const auto& meshes = model.getMeshes();
	if (mBoundModel == &model && meshes.size() == mMeshes.size()
		&& std::equal(meshes.begin(), meshes.end(), mMeshes.begin(), [](const auto& mesh, const auto* current) { return mesh.get() == current; })) {
		return;
	}
	std::unordered_map<std::string, float> retained;
	for (const auto& [name, slots] : mTargets) {
		if (!slots.empty()) {
			retained.emplace(name, mCurrent[slots.front().first][slots.front().second]);
		}
	}
	std::vector<const RENDER::MeshInterface*> resolvedMeshes;
	std::unordered_map<std::string, std::vector<TargetSlot>> resolvedTargets;
	std::vector<std::array<float, 64>> current(meshes.size());
	for (std::size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
		resolvedMeshes.push_back(meshes[meshIndex].get());
		const auto& morphs = meshes[meshIndex]->morphTargets();
		for (std::size_t targetIndex = 0; targetIndex < morphs.size(); ++targetIndex) {
			const auto& target = morphs[targetIndex];
			resolvedTargets[target.Name].push_back({ meshIndex, targetIndex });
			current[meshIndex][targetIndex] = retained.contains(target.Name) ? retained.at(target.Name) : target.DefaultWeight;
		}
	}
	std::unordered_set<std::string> configuredWeights;
	for (const auto& setting : mDescriptor.Weights) {
		if (!std::isfinite(setting.Value) || setting.Value < 0 || setting.Value > 1 || !configuredWeights.insert(setting.Target).second
			|| !resolvedTargets.contains(setting.Target)) {
			throw std::invalid_argument("Invalid or unknown facial weight: " + setting.Target);
		}
		if (!retained.contains(setting.Target)) {
			for (const auto [meshIndex, targetIndex] : resolvedTargets.at(setting.Target)) {
				current[meshIndex][targetIndex] = setting.Value;
			}
		}
	}
	std::vector<ResolvedClip> clips;
	std::unordered_map<std::string, std::size_t> clipNames;
	for (const auto& source : mDescriptor.Clips) {
		if (source.Name.empty() || !clipNames.emplace(source.Name, clips.size()).second || !std::isfinite(source.Duration) || source.Duration <= 0) {
			throw std::invalid_argument("Invalid facial clip name or duration");
		}
		ResolvedClip clip;
		clip.Duration = source.Duration;
		clip.Loop = source.Loop;
		std::unordered_set<std::string> trackNames;
		for (const auto& track : source.Tracks) {
			if (!resolvedTargets.contains(track.Target) || !trackNames.insert(track.Target).second || track.Keys.empty()) {
				throw std::invalid_argument("Invalid facial clip target: " + track.Target);
			}
			float previousTime = -1;
			for (const auto& key : track.Keys) {
				if (!std::isfinite(key.Time) || !std::isfinite(key.Value) || key.Time <= previousTime || key.Time < 0 || key.Time > source.Duration
					|| key.Value < 0 || key.Value > 1) {
					throw std::invalid_argument("Invalid facial clip key for: " + track.Target);
				}
				previousTime = key.Time;
			}
			clip.Tracks.push_back({ resolvedTargets.at(track.Target), track.Keys });
		}
		clips.push_back(std::move(clip));
	}
	for (const auto& source : model.morphAnimations()) {
		if (clipNames.contains(source.Name)) {
			continue;
		}
		ResolvedClip clip;
		clip.Duration = source.Duration;
		clip.Loop = true;
		for (const auto& track : source.Tracks) {
			if (track.MeshIndex >= meshes.size() || track.TargetIndex >= meshes[track.MeshIndex]->morphTargets().size() || track.Keys.empty()) {
				throw std::invalid_argument("Invalid imported facial clip: " + source.Name);
			}
			ResolvedTrack resolved;
			resolved.Slots.push_back({ track.MeshIndex, track.TargetIndex });
			for (const auto& key : track.Keys) {
				resolved.Keys.push_back({ key.Time, key.Weight });
			}
			clip.Tracks.push_back(std::move(resolved));
		}
		clipNames.emplace(source.Name, clips.size());
		clips.push_back(std::move(clip));
	}
	if (!mDescriptor.ActiveClip.empty() && !clipNames.contains(mDescriptor.ActiveClip)) {
		throw std::invalid_argument("Unknown active facial clip: " + mDescriptor.ActiveClip);
	}
	mBoundModel = &model;
	mMeshes = std::move(resolvedMeshes);
	mTargets = std::move(resolvedTargets);
	mCurrent = std::move(current);
	mPrevious = mCurrent;
	mClipNames = std::move(clipNames);
	mClips = std::move(clips);
	mActiveClip = mDescriptor.ActiveClip.empty() ? SIZE_MAX : mClipNames.at(mDescriptor.ActiveClip);
	mTime = 0;
}
void FacialAnimationComponent::setWeight(const std::string& name, float value) {
	if (const auto model = obj->getComponent<ModelRenderer>(); model && model->getModel()) {
		bind(*model->getModel());
	}
	if (!std::isfinite(value) || value < 0 || value > 1) {
		throw std::invalid_argument("Facial weight must be in [0, 1]");
	}
	const auto found = mTargets.find(name);
	if (found == mTargets.end()) {
		throw std::invalid_argument("Unknown facial target: " + name);
	}
	mActiveClip = SIZE_MAX;
	mDescriptor.ActiveClip.clear();
	auto setting = std::find_if(mDescriptor.Weights.begin(), mDescriptor.Weights.end(), [&](const Weight& weight) { return weight.Target == name; });
	if (setting == mDescriptor.Weights.end()) {
		mDescriptor.Weights.push_back({ name, value });
	}
	else {
		setting->Value = value;
	}
	for (const auto [meshIndex, targetIndex] : found->second) {
		mCurrent[meshIndex][targetIndex] = value;
	}
}
float FacialAnimationComponent::weight(const std::string& name) const {
	const auto found = mTargets.find(name);
	if (found == mTargets.end() || found->second.empty()) {
		throw std::invalid_argument("Unknown facial target: " + name);
	}
	const auto [meshIndex, targetIndex] = found->second.front();
	return mCurrent[meshIndex][targetIndex];
}
void FacialAnimationComponent::clearWeights() {
	mActiveClip = SIZE_MAX;
	mDescriptor.ActiveClip.clear();
	mDescriptor.Weights.clear();
	for (std::size_t meshIndex = 0; meshIndex < mMeshes.size(); ++meshIndex) {
		const auto& targets = mMeshes[meshIndex]->morphTargets();
		for (std::size_t targetIndex = 0; targetIndex < targets.size(); ++targetIndex) {
			mCurrent[meshIndex][targetIndex] = targets[targetIndex].DefaultWeight;
		}
	}
}
void FacialAnimationComponent::play(const std::string& name) {
	if (const auto model = obj->getComponent<ModelRenderer>(); model && model->getModel()) {
		bind(*model->getModel());
	}
	const auto found = mClipNames.find(name);
	if (found == mClipNames.end()) {
		throw std::invalid_argument("Unknown facial clip: " + name);
	}
	mActiveClip = found->second;
	mDescriptor.ActiveClip = name;
	mTime = 0;
}
void FacialAnimationComponent::stop() {
	mActiveClip = SIZE_MAX;
	mDescriptor.ActiveClip.clear();
}
std::vector<std::string> FacialAnimationComponent::availableTargets() const {
	std::vector<std::string> result;
	result.reserve(mTargets.size());
	for (const auto& [name, _] : mTargets) {
		result.push_back(name);
	}
	std::sort(result.begin(), result.end());
	return result;
}
std::vector<std::string> FacialAnimationComponent::availableClips() const {
	std::vector<std::string> result;
	result.reserve(mClipNames.size());
	for (const auto& [name, index] : mClipNames) {
		result.push_back(name);
	}
	std::sort(result.begin(), result.end());
	return result;
}
const std::array<float, 64>* FacialAnimationComponent::weightsFor(const RENDER::MeshInterface& mesh) const {
	const auto found = std::find(mMeshes.begin(), mMeshes.end(), &mesh);
	return found == mMeshes.end() ? nullptr : &mCurrent[std::size_t(found - mMeshes.begin())];
}
const std::array<float, 64>* FacialAnimationComponent::previousWeightsFor(const RENDER::MeshInterface& mesh) const {
	const auto found = std::find(mMeshes.begin(), mMeshes.end(), &mesh);
	return found == mMeshes.end() ? nullptr : &mPrevious[std::size_t(found - mMeshes.begin())];
}
void FacialAnimationComponent::onUpdate(std::chrono::duration<double> elapsed) {
	if (const auto model = obj->getComponent<ModelRenderer>(); model && model->getModel()) {
		bind(*model->getModel());
	}
	mPrevious = mCurrent;
	if (mActiveClip == SIZE_MAX || mActiveClip >= mClips.size()) {
		return;
	}
	const auto& clip = mClips[mActiveClip];
	mTime += std::max(0.f, float(elapsed.count()));
	const float sampleTime = clip.Loop ? std::fmod(mTime, clip.Duration) : std::min(mTime, clip.Duration);
	for (const auto& track : clip.Tracks) {
		const auto after
			= std::upper_bound(track.Keys.begin(), track.Keys.end(), sampleTime, [](float time, const Key& key) { return time < key.Time; });
		float value = track.Keys.front().Value;
		if (after == track.Keys.end()) {
			value = track.Keys.back().Value;
		}
		else if (after != track.Keys.begin()) {
			const auto& next = *after;
			const auto& previous = *(after - 1);
			const float alpha = (sampleTime - previous.Time) / (next.Time - previous.Time);
			value = previous.Value + (next.Value - previous.Value) * alpha;
		}
		for (const auto [meshIndex, targetIndex] : track.Slots) {
			mCurrent[meshIndex][targetIndex] = value;
		}
	}
}
void FacialAnimationComponent::onDisable() {
	mActiveClip = SIZE_MAX;
}
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::FacialAnimationComponent>();
	return true;
}();
}
