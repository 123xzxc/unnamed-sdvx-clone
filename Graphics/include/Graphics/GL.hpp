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
	feature level as the existing EMBEDDED renderer path (ES 2.0 + GLSL ES 1.00).
	There is no EGL on iOS: the context is created by SDL through EAGL.
*/
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
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

