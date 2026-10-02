/*
	OpenGL include file
	This file includes the appropriate opengl headers for the platform
*/
#pragma once

// Include platform specific OpenGL headers
#ifdef _WIN32
#include <GL/glew.h>
#include <GL/wglew.h>
#elif defined(USC_IOS)
/*
	iPadOS/iOS.

	OpenGL ES is the only GL flavour available, so the iOS build uses the same
	renderer path as embedded targets (GLSL ES 1.00 shaders). There is no EGL on
	iOS: the context is created by SDL through EAGL.

	The SDK only ships the OpenGL ES 2.0 headers, which declare the ES 2.0 subset.
	The context SDL creates on iOS is an ES 3.0 one (see Graphics/OpenGL.cpp), and
	the embedded renderer uses a few ES 3.0 entry points and enumerants, so the
	ones the ES 2.0 headers leave out are declared here.
*/
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>

// ES 3.0 sized internal format and framebuffer targets.
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_MIRRORED_REPEAT
#define GL_MIRRORED_REPEAT 0x8370
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif

/*
	Vertex array objects are core in ES 3.0 and were promoted from
	GL_OES_vertex_array_object, which is what the ES 2.0 headers declare. The OES
	entry points are the same functions on iOS, so they are used directly.
*/
#ifndef glGenVertexArrays
#define glGenVertexArrays glGenVertexArraysOES
#define glBindVertexArray glBindVertexArrayOES
#define glDeleteVertexArrays glDeleteVertexArraysOES
#endif
#elif __APPLE__
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#elif EMBEDDED
#include "GLES3/gl3.h"
#include "GLES3/gl3ext.h"
#include "EGL/egl.h"
#include "EGL/eglext.h"
#elif __linux
#include <GL/glew.h>
#include <GL/glxew.h>
#endif

