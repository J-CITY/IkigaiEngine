#include "audioSource.h"

#include "audioManager.h"
#include "audioMixerSource.h"
#include <utility>

using namespace IKIGAI;
using namespace IKIGAI::AUDIO;

SoundResource::SoundResource(SoundResource&& other) noexcept
	: id(std::exchange(other.id, std::nullopt))
	, path(std::move(other.path))
	, data(std::move(other.data))
	, mixer(std::move(other.mixer))
	, clip(std::move(other.clip)) { }

SoundResource::~SoundResource() {
	if (id) {
		AudioManagerAL::GetInstance().stop(*this);
	}
}

unsigned SoundResource::getBusId() {
	return mixer ? mixer->getIdBus() : 0;
}

void SoundResource::swapReloadedSource(SoundResource& replacement) noexcept {
	using std::swap;
	swap(data, replacement.data);
	swap(path, replacement.path);
	swap(id, replacement.id);
	clip.swap(replacement.clip);
}
