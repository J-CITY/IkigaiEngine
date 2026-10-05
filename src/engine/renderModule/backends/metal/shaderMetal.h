#pragma once
#ifdef METAL_BACKEND

#include "../interface/shaderInterface.h"
#include "metalBindings.h"
#import <Metal/Metal.h>

namespace IKIGAI::RENDER {

	class ShaderMetal : public ShaderInterface {
	public:
		explicit ShaderMetal(const ShaderResource& res);
		ShaderMetal(const std::string& vertexPath, const std::string& fragmentPath);
		~ShaderMetal() override;

		void bind() override;
		void unbind() override;
		void recompile(const ShaderResource& res) override;

		void setUniform(const std::string& name, int value);
		void setUniform(const std::string& name, float value);
		void setUniform(const std::string& name, const MATH::Vector2f& value);
		void setUniform(const std::string& name, const MATH::Vector3f& value);
		void setUniform(const std::string& name, const MATH::Vector4f& value);
		void setUniform(const std::string& name, const MATH::Matrix3f& value);
		void setUniform(const std::string& name, const MATH::Matrix4f& value);
		void setUniform(const std::string& name, bool value);
		void setUniform(const std::string& name, const std::vector<MATH::Matrix4f>& values);

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

	private:
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
