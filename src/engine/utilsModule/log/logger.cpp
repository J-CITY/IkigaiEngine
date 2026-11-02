#include "logger.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <sstream>

#ifndef NDEBUG
#ifdef __ANDROID__
#include <android/log.h>
#endif
#endif

using namespace IKIGAI::UTILS::LOGG;

Log::Log(const Logger& logger, Level level)
	: mLogger(logger)
	, mLevel(level)
	, mActive(false) {
	if (level >= logger.getLevel()) {
		mStream.emplace();
		mActive = true;
	}
}

Log::Log(Log&& other)
	: mLogger(other.mLogger)
	, mLevel(other.mLevel)
	, mActive(other.mActive)
	, mStream(std::move(other.mStream)) {
	other.mActive = false;
	other.mStream.reset();
}

// Destructor : output the Log string stream
Log::~Log() {
	if (!mActive) {
		return;
	}
	mActive = false;
	mLogger.output(*this);
}

std::string Log::ToString(Log::Level aLevel) {
	std::string pString;

	switch (aLevel) {
	case Level::Debug:
		pString = "DEBUG";
		break;
	case Level::Info:
		pString = "INFO";
		break;
	case Level::Notice:
		pString = "NOTE";
		break;
	case Level::Warning:
		pString = "WARNING";
		break;
	case Level::Error:
		pString = "ERROR";
		break;
	case Level::Critic:
		pString = "CRITICAL";
		break;
	default:
		break;
	}

	return pString;
}

Log::Level Log::ToLevel(const std::string& levelStr) {
	auto level = Level::Critic;
	if (levelStr == "DEBUG") {
		level = Level::Debug;
	}
	else if (levelStr == "INFO") {
		level = Level::Info;
	}
	else if (levelStr == "NOTE") {
		level = Level::Notice;
	}
	else if (levelStr == "WARNING") {
		level = Level::Warning;
	}
	else if (levelStr == "ERROR") {
		level = Level::Error;
	}
	return level;
}

const std::string& OutputConsole::name() const {
	static std::string name = "OutputConsole";
	return name;
}

Modifier getModifier(Log::Level level) {
	switch (level) {
	case Log::Level::Debug:
		return Modifier(Modifier::Code::FG_DEFAULT);
	case Log::Level::Info:
		return Modifier(Modifier::Code::FG_GREEN);
	case Log::Level::Notice:
		return Modifier(Modifier::Code::FG_GREEN);
	case Log::Level::Warning:
		return Modifier(Modifier::Code::FG_DEFAULT);
	case Log::Level::Error:
		return Modifier(Modifier::Code::FG_YELLOW);
	case Log::Level::Critic:
		return Modifier(Modifier::Code::FG_RED);
	default:
		break;
	}
	return Modifier(Modifier::Code::FG_DEFAULT);
}

Modifier getModifierDefault() {
	return { Modifier::Code::FG_DEFAULT };
}

void OutputConsole::output(const Channel::Ptr& aChannelPtr, const Log& aLog) const {
#ifndef NDEBUG
	// const std::time_t curDateTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	// const auto curDateTimeStr = std::to_string(curDateTime);
#ifdef __ANDROID__
	// Android doesn't emit the standard output streams by default, so instead we will
	// pump the logging through the Android Logcat system. If you enter 'a-simple-triangle'
	// as the Logcat filter you will see only these logging statements.
	__android_log_print(ANDROID_LOG_DEBUG, "IKIGAI", "%s:%s: %s", aChannelPtr->getName().c_str(), Log::ToString(aLog.getLevel()).c_str(),
		(aLog.getStream()).str().c_str());
#else
	std::cout << getModifier(aLog.getLevel()) << aChannelPtr->getName() << ":" << Log::ToString(aLog.getLevel()) << ": "
			  << (aLog.getStream()).str().c_str() << getModifierDefault() << std::endl;
#endif
#endif
}

Logger::Logger(const std::string& channelName) {
	mChannelPtr = Manager::Get(channelName);
	assert(mChannelPtr);
}

Logger::~Logger() = default;

Log Logger::debug() const {
	return Log(*this, Log::Level::Debug);
}
Log Logger::info() const {
	return Log(*this, Log::Level::Info);
}
Log Logger::notice() const {
	return Log(*this, Log::Level::Notice);
}
Log Logger::warning() const {
	return Log(*this, Log::Level::Warning);
}
Log Logger::error() const {
	return Log(*this, Log::Level::Error);
}
Log Logger::critic() const {
	return Log(*this, Log::Level::Critic);
}

void Logger::output(const Log& aLog) const {
	Manager::Output(mChannelPtr, aLog);
}

void Manager::ClearOutputs() {
	std::lock_guard<std::mutex> lock(mMutex);
	mOutputList.clear();
}

Channel::Ptr Manager::Get(const std::string& apChannelName) {
	std::lock_guard<std::mutex> lock(mMutex);
	const auto iChannelPtr = mChannelMap.find(apChannelName);
	if (mChannelMap.end() != iChannelPtr) {
		return iChannelPtr->second;
	}
	Channel::Ptr channelPtr(new Channel(apChannelName, mDefaultLevel));
	mChannelMap[apChannelName] = channelPtr;
	return channelPtr;
}

void Manager::Output(const Channel::Ptr& aChannelPtr, const Log& aLog) {
	std::vector<Output::Ptr> outputs;
	{
		std::lock_guard<std::mutex> lock(mMutex);
		outputs = mOutputList;
	}
	for (const auto& out : outputs) {
		out->output(aChannelPtr, aLog);
	}
}
