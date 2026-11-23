#include "support/expect.h"

#include "particleModule/particles.h"

#include <memory>

using IKIGAI::TEST::expect;

int main() {
	using IKIGAI::PARTICLES::Definition;
	using IKIGAI::PARTICLES::EmitterDefinition;
	using IKIGAI::PARTICLES::Runtime;

	expect(IKIGAI::PARTICLES::hash("spark") == IKIGAI::PARTICLES::hash("spark"), "particles.hash.stable");
	expect(IKIGAI::PARTICLES::hash("spark") != IKIGAI::PARTICLES::hash("smoke"), "particles.hash.differs");
	const float sample = IKIGAI::PARTICLES::random01(42);
	expect(sample >= 0.f && sample < 1.f, "particles.random.unit");
	expect(sample == IKIGAI::PARTICLES::random01(42), "particles.random.stable");

	auto definition = std::make_shared<Definition>();
	definition->dimension = 2;
	EmitterDefinition emitter;
	emitter.id = "spark";
	emitter.capacity = 8;
	definition->emitters.push_back(emitter);

	Runtime<2> runtime(definition, 7);
	expect(!runtime.isAlive(), "particles.empty");
	runtime.emit(0, 3);
	expect(runtime.emitters().at(0).particles.size() == 3, "particles.emit.count");
	expect(runtime.isAlive(), "particles.emit.alive");
	const auto age = runtime.emitters().at(0).particles.at(0).age;
	runtime.update(0.1);
	expect(runtime.emitters().at(0).particles.at(0).age > age, "particles.update.ages");
	return IKIGAI::TEST::failureCount() == 0 ? 0 : 1;
}
