#pragma once
#include <chrono>
#include <memory>

#include "core.h"
#include <utilsModule/ref.h>

namespace IKIGAI {
namespace CORE {
	class App {
	public:
		explicit App(const PlatformStartup& platform);
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
		std::chrono::duration<double> m_fixedTimeAccumulator {};

	private:
		struct RenderTransformStaging;
		std::unique_ptr<RenderTransformStaging> mRenderTransformStaging;
	};
}
}
