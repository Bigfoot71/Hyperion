/**
 * Copyright (c) 2025 Le Juez Victor
 *
 * This software is provided "as-is", without any express or implied warranty. In no event
 * will the authors be held liable for any damages arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose, including commercial
 * applications, and to alter it and redistribute it freely, subject to the following restrictions:
 *
 *   1. The origin of this software must not be misrepresented; you must not claim that you
 *   wrote the original software. If you use this software in a product, an acknowledgment
 *   in the product documentation would be appreciated but is not required.
 *
 *   2. Altered source versions must be plainly marked as such, and must not be misrepresented
 *   as being the original software.
 *
 *   3. This notice may not be removed or altered from any source distribution.
 */

#include "./HP_CoreState.hpp"

#include "./HP_InternalLog.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <glad/gles2.h>
#include <physfs.h>

#include <stdexcept>

/* === Internal Helper Functions === */

namespace {

void* HP_PhysFS_malloc(PHYSFS_uint64 size)
{
    return SDL_malloc(static_cast<size_t>(size));
}

void* HP_PhysFS_realloc(void* ptr, PHYSFS_uint64 size)
{
    return SDL_realloc(ptr, static_cast<size_t>(size));
}

constexpr PHYSFS_Allocator HP_PhysFS_Allocator = {
    .Init = nullptr,
    .Deinit = nullptr,
    .Malloc = HP_PhysFS_malloc,
    .Realloc = HP_PhysFS_realloc,
    .Free = SDL_free,
};

} // namespace

/* === Private Implementation === */

HP_CoreState::HP_CoreState(const char* title, int w, int h, const HP_AppDesc& desc)
    : mPerfFrequency(SDL_GetPerformanceFrequency())
    , mTargetFrameTime((desc.targetFPS > 0) ? 1.0f / desc.targetFPS : 0.0f)
{
    /* --- Configure log system --- */

    SDL_SetLogPriorityPrefix(SDL_LOG_PRIORITY_TRACE,    "[T] ");
    SDL_SetLogPriorityPrefix(SDL_LOG_PRIORITY_VERBOSE,  "[V] ");
    SDL_SetLogPriorityPrefix(SDL_LOG_PRIORITY_DEBUG,    "[D] ");
    SDL_SetLogPriorityPrefix(SDL_LOG_PRIORITY_INFO,     "[I] ");
    SDL_SetLogPriorityPrefix(SDL_LOG_PRIORITY_WARN,     "[W] ");
    SDL_SetLogPriorityPrefix(SDL_LOG_PRIORITY_ERROR,    "[E] ");
    SDL_SetLogPriorityPrefix(SDL_LOG_PRIORITY_CRITICAL, "[F] ");

    /* --- Defines custom memory functions --- */

    if (desc.memory.malloc && desc.memory.calloc && desc.memory.realloc && desc.memory.free) {
        if (!SDL_SetMemoryFunctions(desc.memory.malloc, desc.memory.calloc, desc.memory.realloc, desc.memory.free)) {
            HP_INTERNAL_LOG(W, "CORE: Failed to set custom memory functions; %s", SDL_GetError());
        }
    }
    else if (desc.memory.malloc || desc.memory.calloc || desc.memory.realloc || desc.memory.free) {
        HP_INTERNAL_LOG(W, "CORE: Failed to set custom memory functions; If you define at least one memory function, they must all be defined", SDL_GetError());
    }

    /* --- Init app metadata --- */

    SDL_SetAppMetadata(desc.name, desc.version, desc.identifier);

    /* --- Init PhysFS --- */

    PHYSFS_setAllocator(&HP_PhysFS_Allocator);
    PHYSFS_init(nullptr);

    PHYSFS_mount(SDL_GetBasePath(), "/", 1);

    /* --- Init SDL stuff --- */

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        throw std::runtime_error(std::string("CORE: Failed to init video subsystem; ") + SDL_GetError());
    }

#ifndef NDEBUG
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_VERBOSE);
#endif

    /* --- Test OpenGL support and define attributes --- */

    bool useOpenGLES = false;

    // Common OpenGL attributes
    auto setCommonGLAttributes = []() {
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 0);
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    };

#if defined(HP_PLATFORM_MACOS) || defined(HP_PLATFORM_ANDROID)
    // Android always uses OpenGL ES
    // Hopefully MacOS via ANGLE
    useOpenGLES = true;
#else
    // Test OpenGL 4.5 support on other platforms
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    setCommonGLAttributes();

    SDL_Window* testWindow = SDL_CreateWindow(nullptr, 1, 1, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (testWindow) {
        SDL_GLContext testContext = SDL_GL_CreateContext(testWindow);
        if (!testContext) {
            HP_INTERNAL_LOG(W, "CORE: OpenGL 4.5 not supported, falling back to OpenGL ES 3.2");
            useOpenGLES = true;
        }
        else {
            SDL_GL_DestroyContext(testContext);
        }
        SDL_DestroyWindow(testWindow);
    }
    else {
        useOpenGLES = true;
    }
#endif

    // Set final OpenGL attributes
    if (useOpenGLES) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    }
    else {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    }
    setCommonGLAttributes();

    /* --- Create the SDL window --- */

    mWindow = SDL_CreateWindow(title, w, h, SDL_WINDOW_OPENGL | sdlWindowFlags(desc.flags));
    if (!mWindow) {
        throw std::runtime_error(std::string("CORE: Failed to create window; ") + SDL_GetError());
    }

    SDL_SetWindowPosition(mWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    /* --- Create OpenGL context --- */

    mGLContext = SDL_GL_CreateContext(mWindow);
    if (!mGLContext) {
        throw std::runtime_error(std::string("CORE: Failed to create OpenGL context; ") + SDL_GetError());
    }

    /* --- Load OpenGL functions --- */

    if (gladLoadGLES2(SDL_GL_GetProcAddress) < 0) {
        throw std::runtime_error("CORE: Failed to load OpenGL functions");
    }

    /* --- Store GL context type --- */

    mGLProfile = useOpenGLES ? SDL_GL_CONTEXT_PROFILE_ES : SDL_GL_CONTEXT_PROFILE_CORE;

    /* --- Set VSync --- */

    if (desc.flags & HP_FLAG_VSYNC_HINT) {
        if (!SDL_GL_SetSwapInterval(-1)) {
            SDL_GL_SetSwapInterval(1);
        }
    }
    else {
        SDL_GL_SetSwapInterval(0);
    }

    /* --- Print debug infos --- */

    HP_INTERNAL_LOG(D, "CORE: GL Vendor     : %s", glGetString(GL_VENDOR));
    HP_INTERNAL_LOG(D, "CORE: GL Renderer   : %s", glGetString(GL_RENDERER));
    HP_INTERNAL_LOG(D, "CORE: GL Version    : %s", glGetString(GL_VERSION));
    HP_INTERNAL_LOG(D, "CORE: GLSL Version  : %s", glGetString(GL_SHADING_LANGUAGE_VERSION));
}

HP_CoreState::~HP_CoreState()
{
    if (mGLContext) {
        SDL_GL_DestroyContext(mGLContext);
        mGLContext = NULL;
    }

    if (mWindow) {
        SDL_DestroyWindow(mWindow);
        mWindow = NULL;
    }

    SDL_Quit();
}
