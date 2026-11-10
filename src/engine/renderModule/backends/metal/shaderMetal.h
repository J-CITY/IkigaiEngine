#pragma once
#ifdef METAL_BACKEND

#include "../interface/shaderInterface.h"
#include "metalBindings.h"
#include <optional>
#import <Metal/Metal.h>

namespace IKIGAI::RENDER {

	class ShaderMetal : public ShaderInterface {
	public:
		struct ArgumentIds { uint32_t resource=0, sampler=0, count=1; };
		std::map<ShaderType,std::map<uint32_t,ArgumentIds>> mArgumentIds;
		id<MTLFunction> function(ShaderType stage) const {
			if (stage==ShaderType::VERTEX) return rasterVertexFunction();
			if (stage==ShaderType::FRAGMENT) return mFragmentFunction;
			if (stage==ShaderType::COMPUTE) return mComputeFunction;
			return nil;
		}
		explicit ShaderMetal(const ShaderResource& res);
		ShaderMetal(const std::string& vertexPath, const std::string& fragmentPath);
		~ShaderMetal() override;

		void bind() override;
		void unbind() override;
		void recompile(const ShaderResource& res) override;
		bool adoptReloadedShader(ShaderInterface& replacement) noexcept override;

		void setUniform(const std::string& name, int value);
		void setUniform(const std::string& name, float value);
		void setUniform(const std::string& name, const MATH::Vector2f& value);
		void setUniform(const std::string& name, const MATH::Vector3f& value);
		void setUniform(const std::string& name, const MATH::Vector4f& value);
		void setUniform(const std::string& name, const MATH::Matrix3f& value);
		void setUniform(const std::string& name, const MATH::Matrix4f& value);
		void setUniform(const std::string& name, bool value);
		void setUniform(const std::string& name, const std::vector<MATH::Matrix4f>& values);

		id<MTLFunction> getComputeFunction() const { return mComputeFunction; }
		id<MTLFunction> getVertexFunction() const { return mVertexFunction; }
		id<MTLFunction> getFragmentFunction() const { return mFragmentFunction; }
		id<MTLFunction> getGeometryFunction() const { return mGeometryFunction; }
		id<MTLFunction> getTessellationControlFunction() const { return mTessControlFunction; }
		id<MTLFunction> getTessellationEvaluationFunction() const { return mTessEvalFunction; }
		id<MTLFunction> getTessellationVertexFunction(bool indexed) const {
			return indexed ? mTessVertexIndexedFunction : mTessVertexFunction;
		}
		id<MTLFunction> rasterVertexFunction() const;

		bool hasTessellation() const { return mTessEvalFunction != nil; }
		bool hasGeometry() const { return mGeometryFunction != nil; }
		uint32_t tessellationControlPoints() const { return mTessControlPoints; }
		bool tessellationTriangles() const { return mTessTriangles; }
		MTLTessellationPartitionMode tessellationPartition() const { return mTessPartition; }
		size_t getGeneration() const { return mGeneration; }
		std::optional<uint32_t> samplerBinding(ShaderType stage, uint32_t textureBinding) const {
			auto stages = mSamplerBindings.find(stage);
			if (stages == mSamplerBindings.end()) return {};
			auto sampler = stages->second.find(textureBinding);
			return sampler == stages->second.end() ? std::optional<uint32_t>{} : sampler->second;
		}

	private:
		id<MTLFunction> mComputeFunction = nil;
		id<MTLFunction> mVertexFunction = nil;
		id<MTLFunction> mFragmentFunction = nil;
		id<MTLFunction> mGeometryFunction = nil;
		id<MTLFunction> mTessControlFunction = nil;
		id<MTLFunction> mTessEvalFunction = nil;
		id<MTLFunction> mTessVertexFunction = nil;
		id<MTLFunction> mTessVertexIndexedFunction = nil;
		uint32_t mTessControlPoints = 3;
		bool mTessTriangles = true;
		MTLTessellationPartitionMode mTessPartition = MTLTessellationPartitionModeFractionalOdd;
		size_t mGeneration = 0;
		std::map<ShaderType, std::map<uint32_t, uint32_t>> mSamplerBindings;

		struct StageResult {
			id<MTLFunction> function = nil;
			uint32_t outputVertices = 0;
			bool triangles = true;
			MTLTessellationPartitionMode partition = MTLTessellationPartitionModeFractionalOdd;
		};

		void create(const ShaderResource& res);
		StageResult compileStage(const std::vector<uint32_t>& spirv, ShaderType stage, bool vertexForTessellation, bool indexedVertex);
	};

}

#endif
