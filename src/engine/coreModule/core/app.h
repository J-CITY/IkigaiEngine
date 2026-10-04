#pragma once
#include <chrono>

#include "core.h"
#include <utilsModule/ref.h>
#ifdef OCULUS
#include <android_native_app_glue.h>
#endif
#ifdef DX12_BACKEND
#include <Windows.h>
#endif

namespace IKIGAI {
	namespace CORE {
		class App {
		public:
#ifdef DX12_BACKEND
			inline static HINSTANCE hInstance;
#endif
			App(
#ifdef OCULUS
			android_app* app
#endif
			);
			~App();
			void run();
			bool runMainLoop();
			[[nodiscard]] bool isRunning() const;
			void preUpdate(std::chrono::duration<double> dt);
			void update(std::chrono::duration<double> dt);
			void postUpdate(std::chrono::duration<double> dt);
			UTILS::Ref<Core> getCore();
		protected:
			Core core;
			std::chrono::duration<double> m_fixedTimeAccumulator{};
		};
	}
}
