#pragma once

#include "logger.h"
#include "resourceModule/serviceManager.h"

#include <iostream>
#include <sstream>

#ifdef __ANDROID__
#include <android/log.h>
#define IKIGAI_COUT_ANDROID(text) __android_log_print(ANDROID_LOG_INFO, "IKIGAI", "%s", (text).c_str())
#else
#define IKIGAI_COUT_ANDROID(text) ((void)0)
#endif

#define IKIGAI_COUT(expr)                         \
	do {                                          \
		std::ostringstream _ikigaiCout;           \
		_ikigaiCout << expr;                      \
		std::cout << _ikigaiCout.str() << std::endl; \
		std::cout.flush();                        \
		IKIGAI_COUT_ANDROID(_ikigaiCout.str());   \
	} while (0)

#define LOG_INFO IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::UTILS::LOGG::Logger>().info()
#define LOG_WARNING IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::UTILS::LOGG::Logger>().warning()
#define LOG_ERROR IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::UTILS::LOGG::Logger>().error()
