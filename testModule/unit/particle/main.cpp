#include "support/expect.h"

#include "particleModule/particles.h"

#include <array>
#include <memory>
#include <stdexcept>

using IKIGAI::TEST::expect;
using IKIGAI::TEST::expectNear;
using IKIGAI::TEST::expectThrows;
using IKIGAI::TEST::require;

namespace {
using IKIGAI::PARTICLES::Definition;
using IKIGAI::PARTICLES::EmitterDefinition;
using IKIGAI::PARTICLES::Module;
using IKIGAI::PARTICLES::ModuleType;
using IKIGAI::PARTICLES::Runtime;
using IKIGAI::PARTICLES::SourceMode;
using IKIGAI::PARTICLES::State;

std::shared_ptr<Definition> makeDefinition(std::uint32_t capacity, float rate = 0.f) {
	auto definition = std::make_shared<Definition>();
	definition->dimension = 2;
	EmitterDefinition emitter;
	emitter.id = "spark";
	emitter.capacity = capacity;
	emitter.duration = 1.0;
	emitter.rate.mode = SourceMode::Constant;
	emitter.rate.channels = 1;
	emitter.rate.min[0] = rate;
	definition->emitters.push_back(emitter);
	return definition;
}

void checkBasics() {
	Runtime<2> runtime(makeDefinition(8), 7);
	expect(!runtime.isAlive() && runtime.state() == State::Playing, "particles.initial");
	runtime.emit(0, 3);
	require(runtime.emitters().at(0).particles.size() == 3, "particles.emit.count");
	const auto firstAge = runtime.emitters().at(0).particles.at(0).age;
	runtime.pause();
	runtime.update(0.25);
	expect(runtime.state() == State::Paused, "particles.pause.state");
	require(runtime.emitters().at(0).particles.size() == 3, "particles.pause.keepsParticles");
	expect(runtime.emitters().at(0).particles.at(0).age == firstAge, "particles.pause.freezes");
	runtime.resume();
	runtime.update(0.25);
	expect(runtime.emitters().at(0).particles.at(0).age > firstAge, "particles.resume.ages");

	runtime.stop(true);
	expect(!runtime.isAlive(), "particles.stop.clears");

	auto mortal = makeDefinition(4);
	Module initialize;
	initialize.type = ModuleType::Initialize;
	initialize.lifetime.mode = SourceMode::Constant;
	initialize.lifetime.channels = 1;
	initialize.lifetime.min[0] = 1.f / 30.f;
	mortal->emitters[0].spawn.push_back(initialize);
	Runtime<2> dying(mortal, 5);
	dying.emit(0, 2);
	dying.update(0.1);
	expect(dying.emitters().at(0).particles.empty() && !dying.isAlive(), "particles.lifetime.expires");

	Runtime<2> limited(makeDefinition(2), 1);
	limited.emit(0, 5);
	expect(limited.emitters().at(0).particles.size() == 2, "particles.capacity.keeps");
	expect(limited.emitters().at(0).dropped == 3, "particles.capacity.drops");

	expectThrows<std::invalid_argument>([] { Runtime<2>(makeDefinition(1), 1).update(-1.0); }, "particles.negativeDt");
	expectThrows<std::invalid_argument>([] { Runtime<2>(makeDefinition(1), 1).emit(4, 1); }, "particles.badEmitter");
	expectThrows<std::invalid_argument>([] { Runtime<3>(makeDefinition(1), 1); }, "particles.dimension");
	auto restarted = makeDefinition(4);
	Runtime<2> again(restarted, 9);
	again.emit(0, 2);
	again.restart(11);
	expect(!again.isAlive(), "particles.restart.clears");
}

void checkEmissionAndDrain() {
	auto definition = makeDefinition(8, 30.f);
	Module initialize;
	initialize.type = ModuleType::Initialize;
	initialize.lifetime.min[0] = 0.1f;
	definition->emitters.at(0).spawn.push_back(initialize);
	Runtime<2> runtime(definition, 3);
	const auto& particles = runtime.emitters().at(0).particles;
	for (const auto expected : std::array<std::size_t, 4> { 0, 1, 1, 2 }) {
		runtime.update(Runtime<2>::Step);
		expect(particles.size() == expected, "particles.rate.carriesFractionBetweenSteps");
	}
	runtime.stop(false);
	expect(runtime.state() == State::Draining && particles.size() == 2, "particles.drain.keepsLiveParticles");
	runtime.update(2 * Runtime<2>::Step);
	expect(runtime.state() == State::Draining && particles.size() == 2, "particles.drain.doesNotSpawnOrClearEarly");
	runtime.update(7 * Runtime<2>::Step);
	expect(runtime.isComplete() && !runtime.isAlive() && particles.empty(), "particles.drain.finishesWhenLastParticleDies");
}

void checkSeededMotion3D() {
	auto definition = makeDefinition(8);
	definition->dimension = 3;
	Module initialize;
	initialize.type = ModuleType::Initialize;
	initialize.lifetime.min[0] = 1.f;
	initialize.velocity.mode = SourceMode::Uniform;
	initialize.velocity.channels = 3;
	initialize.velocity.min = { 1.f, 2.f, 3.f };
	initialize.velocity.max = { 4.f, 2.f, 3.f };
	Module integrate;
	integrate.type = ModuleType::IntegrateMotion;
	definition->emitters.at(0).spawn.push_back(initialize);
	definition->emitters.at(0).update.push_back(integrate);

	const auto simulate = [](Runtime<3>& runtime) {
		runtime.emit(0, 4);
		runtime.update(2 * Runtime<3>::Step);
		return runtime.emitters().at(0).particles;
	};
	Runtime<3> first(definition, 41);
	Runtime<3> second(definition, 41);
	Runtime<3> different(definition, 87);
	const auto reference = simulate(first);
	const auto repeated = simulate(second);
	const auto otherSeed = simulate(different);
	first.restart(41);
	const auto restarted = simulate(first);
	require(reference.size() == 4 && repeated.size() == 4 && otherSeed.size() == 4 && restarted.size() == 4, "particles.seeded.expectedCount");
	bool variation = false;
	for (std::size_t index = 0; index < reference.size(); ++index) {
		const auto& particle = reference[index];
		expect(particle.position == repeated[index].position && particle.velocity == repeated[index].velocity && particle.age == repeated[index].age,
			"particles.seeded.sameSeedReplaysSimulation");
		expect(particle.position == restarted[index].position && particle.velocity == restarted[index].velocity && particle.id == restarted[index].id,
			"particles.seeded.restartReplaysSimulation");
		expectNear(particle.position[2], 0.1f, "particles.motion3d.integratesZ");
		variation = variation || particle.velocity[0] != otherSeed[index].velocity[0];
	}
	expect(variation, "particles.seeded.differentSeedChangesSampledVelocity");
}
}

int main(int argc, char** argv) {
	return IKIGAI::TEST::runCases(
		argc, argv, { { "basics", checkBasics }, { "emission_and_drain", checkEmissionAndDrain }, { "seeded_motion_3d", checkSeededMotion3D } });
}
