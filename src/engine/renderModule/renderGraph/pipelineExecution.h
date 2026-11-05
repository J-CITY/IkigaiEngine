#pragma once
#include "submissionMode.h"
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <serdepp/attribute/default.hpp>
#include <serdepp/serializer.hpp>
#include <stdexcept>
#include <type_traits>

namespace IKIGAI::RENDER {
inline constexpr unsigned MaxPipelineFramesInFlight = 4;

struct PipelineExecution {
	std::optional<int> FramesInFlight;
	std::optional<SubmissionMode> DrivenMode;
	bool AsyncCompute = false;

	template <class Context> static constexpr auto serde(Context& context, PipelineExecution& value) {
		if constexpr (std::is_same_v<typename Context::Adaptor, nlohmann::json>) {
			if constexpr (Context::is_serialize) {
				if (context.adaptor.is_null()) {
					context.adaptor = Context::Adaptor::object();
				}
			}
			else {
				if (!context.adaptor.is_object()) {
					throw std::invalid_argument("Pipeline Execution must be an object");
				}
				const auto frames = context.adaptor.find("FramesInFlight");
				if (frames != context.adaptor.end() && (!frames->is_number_integer() || *frames < 1 || *frames > MaxPipelineFramesInFlight)) {
					throw std::invalid_argument("Pipeline Execution.FramesInFlight must be an integer between 1 and 4");
				}
			}
		}
		serde::serde_struct(context, value)
			.field(&PipelineExecution::FramesInFlight, "FramesInFlight")
			.field(&PipelineExecution::DrivenMode, "DrivenMode")
			.field(&PipelineExecution::AsyncCompute, "AsyncCompute", serde::attribute::default_ { false });
	}
};
}
