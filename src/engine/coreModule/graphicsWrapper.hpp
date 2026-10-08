#pragma once

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#define USING_GLES
#elif __APPLE__
#define GL_SILENCE_DEPRECATION
#include "TargetConditionals.h"
#if TARGET_OS_IPHONE
#include <OpenGLES/ES2/gl.h>
#define USING_GLES
#else
#include <OpenGL/gl3.h>
#endif
#elif defined(__ANDROID__)
#ifndef IKIGAI_GLES_VERSION
#define IKIGAI_GLES_VERSION 320
#endif
#if IKIGAI_GLES_VERSION >= 320
#include <GLES3/gl32.h>
#elif IKIGAI_GLES_VERSION >= 310
#include <GLES3/gl31.h>
#else
#include <GLES3/gl3.h>
#endif
#define USING_GLES
#if IKIGAI_GLES_VERSION >= 310
#define IKIGAI_GLES_HAS_SSBO 1
#endif
#elif defined(WIN32) || defined(_WIN32) || defined(__linux__) || defined(__unix__)

#ifdef OPENGL_BACKEND
#define GLEW_STATIC
#include <GL/glew.h>
#endif

#ifdef VULKAN_BACKEND
#include <volk.h>
#endif

#endif

// Shader storage buffers are OpenGL 4.3 / GLES 3.1. macOS is 4.1, WebGL2 is ES 3.00.
#if defined(IKIGAI_GLES_HAS_SSBO) || (!defined(USING_GLES) && !defined(__APPLE__))
#define IKIGAI_GL_HAS_SSBO 1
#endif
