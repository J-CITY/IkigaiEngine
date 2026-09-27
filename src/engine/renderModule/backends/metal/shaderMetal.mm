#ifdef METAL_BACKEND

#include "shaderMetal.h"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <spirv_msl.hpp>

namespace IKIGAI::RENDER {

    static std::vector<uint32_t> readFileSpirv(const std::string& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("failed to open file! " + filename);
        }
        size_t fileSize = (size_t)file.tellg();
        std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
        file.seekg(0);
        file.read((char*)buffer.data(), fileSize);
        file.close();
        return buffer;
    }

    ShaderMetal::ShaderMetal(id<MTLDevice> device, const std::string& vertexSource, const std::string& fragmentSource) {
        // vertexSource and fragmentSource are actually paths to SPIRV files (.spv)
        // just like in Vulkan backend.
        
        auto vertSpirv = readFileSpirv(vertexSource);
        auto fragSpirv = readFileSpirv(fragmentSource);
        
        spirv_cross::CompilerMSL vertCompiler(vertSpirv);
        spirv_cross::CompilerMSL fragCompiler(fragSpirv);
        
        spirv_cross::CompilerMSL::Options options;
        options.set_msl_version(2, 1);
        vertCompiler.set_msl_options(options);
        fragCompiler.set_msl_options(options);
        
        std::string vertMsl = vertCompiler.compile();
        std::string fragMsl = fragCompiler.compile();
        
        NSError* error = nil;
        
        NSString* vertNSString = [NSString stringWithUTF8String:vertMsl.c_str()];
        mLibrary = [device newLibraryWithSource:vertNSString options:nil error:&error];
        if (error) {
            std::cout << "Metal Vert Shader Compile Error: " << [[error localizedDescription] UTF8String] << std::endl;
        }
        mVertexFunction = [mLibrary newFunctionWithName:@"main0"];
        
        NSString* fragNSString = [NSString stringWithUTF8String:fragMsl.c_str()];
        id<MTLLibrary> fragLib = [device newLibraryWithSource:fragNSString options:nil error:&error];
        if (error) {
            std::cout << "Metal Frag Shader Compile Error: " << [[error localizedDescription] UTF8String] << std::endl;
        }
        mFragmentFunction = [fragLib newFunctionWithName:@"main0"];
    }

    ShaderMetal::~ShaderMetal() {
        // ARC will handle release of MTL objects
    }

    void ShaderMetal::compileFromGLSL(const std::string& vertexSource, const std::string& fragmentSource) {
        // Obsolete
    }

    void ShaderMetal::bind() {
        // Setting pipeline state is usually done by DriverMetal
    }

    void ShaderMetal::unbind() {}

    void ShaderMetal::setUniform(const std::string& name, int value) {}
    void ShaderMetal::setUniform(const std::string& name, float value) {}
    void ShaderMetal::setUniform(const std::string& name, const MATH::Vector2f& value) {}
    void ShaderMetal::setUniform(const std::string& name, const MATH::Vector3f& value) {}
    void ShaderMetal::setUniform(const std::string& name, const MATH::Vector4f& value) {}
    void ShaderMetal::setUniform(const std::string& name, const MATH::Matrix3f& value) {}
    void ShaderMetal::setUniform(const std::string& name, const MATH::Matrix4f& value) {}
    void ShaderMetal::setUniform(const std::string& name, bool value) {}
    void ShaderMetal::setUniform(const std::string& name, const std::vector<MATH::Matrix4f>& values) {}

}

#endif
