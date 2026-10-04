#include "time.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

using namespace IKIGAI::TIME;

Timer::TimerGenerator tick() {
	std::chrono::steady_clock::time_point current = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point prevTime = current;
	while (true) {
		prevTime = current;
		current = std::chrono::steady_clock::now();
		co_yield current - prevTime;
	}
}

Timer::Timer(): timerGenerator(tick()) {
	init();
}

double Timer::getFPS() const {
	return 1.0 / (dt.count());
}

std::chrono::duration<double> Timer::getDeltaTime() const {
	return dt * scale;
}

std::chrono::duration<double> Timer::getDeltaTimeUnscaled() const {
	return dt;
}

std::chrono::duration<double> Timer::getFixedDeltaTime() const {
	return fixedDt * scale;
}

void Timer::setFixedDeltaTime(std::chrono::duration<double> step) {
	if (step.count() > 0.0) {
		fixedDt = step;
	}
}

std::chrono::duration<double> Timer::getTimeSinceStart() const {
	return std::chrono::steady_clock::now() - start;
}

double Timer::getTimeScale() const {
	return scale;
}

void Timer::setScale(double s) {
	scale = s;
}

Timer& Timer::GetInstance() {
	static Timer singleton;
	return singleton;
}

void Timer::init() {
	start = std::chrono::steady_clock::now();
#ifdef __EMSCRIPTEN__
	dt = std::chrono::duration<double>(1.0 / 60.0);
#endif
}

void Timer::update() {
#ifdef __EMSCRIPTEN__
	const double nowMs = emscripten_get_now();
	if (mLastMs < 0.0) {
		mLastMs = nowMs;
		dt = std::chrono::duration<double>(1.0 / 60.0);
		return;
	}
	double delta = (nowMs - mLastMs) / 1000.0;
	mLastMs = nowMs;
	if (delta < 0.0) {
		delta = 0.0;
	}
	if (delta > 0.1) {
		delta = 0.1;
	}
	dt = std::chrono::duration<double>(delta);
#else
	dt = timerGenerator.h_.promise().value_;
	timerGenerator.h_();
#endif
}
