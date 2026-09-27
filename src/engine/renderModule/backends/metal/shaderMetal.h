#pragma once
#ifdef METAL_BACKEND

#include "../interface/shaderInterface.h"
#import <Metal/Metal.h>

namespace IKIGAI::RENDER {

    class ShaderMetal : public ShaderInterface {
    public:
        ShaderMetal(id<MTLDevice> device, const std::string& vertexSource, const std::string& fragmentSource);
        virtual ~ShaderMetal() override;

        void bind() override;
        void unbind() override;
        
        void setUniform(const std::string& name, int value);
        void setUniform(const std::string& name, float value);
        void setUniform(const std::string& name, const MATH::Vector2f& value);
        void setUniform(const std::string& name, const MATH::Vector3f& value);
        void setUniform(const std::string& name, const MATH::Vector4f& value);
        void setUniform(const std::string& name, const MATH::Matrix3f& value);
        void setUniform(const std::string& name, const MATH::Matrix4f& value);
        void setUniform(const std::string& name, bool value);
        void setUniform(const std::string& name, const std::vector<MATH::Matrix4f>& values);

        void recompile(const ShaderResource& res) override {}

        id<MTLFunction> getVertexFunction() const { return mVertexFunction; }
        id<MTLFunction> getFragmentFunction() const { return mFragmentFunction; }

    private:
        id<MTLFunction> mVertexFunction;
        id<MTLFunction> mFragmentFunction;
        id<MTLLibrary> mLibrary;

        void compileFromGLSL(const std::string& vertexSource, const std::string& fragmentSource);
    };

}

#endif
