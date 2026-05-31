// MobileGlues - gl/sync.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header
//
// Robust GL sync / fence object wrappers for GLES environments.
//
// Problem: Minecraft 1.21.x (NeoForge) uses GL sync objects (glFenceSync /
// glClientWaitSync) via MappableRingBuffer / GlFence to guard shared GPU
// buffers.  On some GLES 3.0 drivers (especially iOS via PojavLauncher /
// ANGLE) glFenceSync can return NULL (0) even though GLES 3.0 formally
// requires sync support.  When that happens, the subsequent
// glClientWaitSync(NULL, ...) call returns 0 – not a valid status code – and
// Minecraft throws:
//
//   IllegalStateException: Failed to complete GPU fence: 0
//
// The fix:
//   1. glFenceSync   – if the driver returns NULL we hand back a "fake"
//                      sentinel pointer so the fence object ID is never 0.
//   2. glClientWaitSync – if the result is 0 or GL_WAIT_FAILED we return
//                         GL_ALREADY_SIGNALED so Minecraft can proceed safely.
//   3. glDeleteSync / glIsSync / glWaitSync / glGetSynciv – all guard against
//                         the fake sentinel.

#include "../includes.h"
#include <GL/gl.h>
#include "glcorearb.h"
#include "log.h"
#include "../gles/loader.h"
#include "mg.h"
#include <GLES3/gl32.h>
#include <atomic>
#include <cstdint>

// ---------------------------------------------------------------------------
// Fake-sync sentinel
// ---------------------------------------------------------------------------
// When the driver returns NULL from glFenceSync we hand back this value
// instead.  We use a non-zero pointer value that can never be a real kernel
// sync object so we can detect it in every wrapper.
//
// NOTE: GLsync is defined as struct __GLsync* in GLES headers, so we cast
// through uintptr_t to avoid strict-aliasing/pointer warnings.

static constexpr uintptr_t FAKE_SYNC_SENTINEL = static_cast<uintptr_t>(0xDEADF001u);

static inline bool is_fake_sync(GLsync sync) {
    return reinterpret_cast<uintptr_t>(sync) == FAKE_SYNC_SENTINEL;
}

static inline GLsync fake_sync() {
    return reinterpret_cast<GLsync>(FAKE_SYNC_SENTINEL);
}

// ---------------------------------------------------------------------------
// glFenceSync
// ---------------------------------------------------------------------------
// Create a GPU fence.  If the underlying GLES driver fails (returns NULL) we
// return the fake sentinel so the caller always gets a non-zero sync handle.

extern "C" GLAPI GLAPIENTRY GLsync glFenceSync(GLenum condition, GLbitfield flags) {
    LOG()
    LOG_D("glFenceSync, condition: 0x%x, flags: 0x%x", condition, flags)

    GLsync sync = GLES.glFenceSync(condition, flags);
    if (sync == nullptr) {
        LOG_W("glFenceSync: driver returned NULL for condition 0x%x – using fake sentinel", condition)
        // Clear any GL error the failed call may have produced.
        GLES.glGetError();
        return fake_sync();
    }
    LOG_D("glFenceSync -> %p", (void*)sync)
    return sync;
}

// ---------------------------------------------------------------------------
// glClientWaitSync
// ---------------------------------------------------------------------------
// Wait on a sync object from the CPU side.  Handles:
//   • Fake sentinel  → return GL_ALREADY_SIGNALED immediately.
//   • Driver returns 0 or GL_WAIT_FAILED → return GL_ALREADY_SIGNALED so
//     Minecraft's GlFence.awaitCompletion() does not throw.

extern "C" GLAPI GLAPIENTRY GLenum glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
    LOG()
    LOG_D("glClientWaitSync, sync: %p, flags: 0x%x, timeout: %llu", (void*)sync, flags, (unsigned long long)timeout)

    if (is_fake_sync(sync)) {
        LOG_D("glClientWaitSync: fake sync – returning GL_ALREADY_SIGNALED")
        return GL_ALREADY_SIGNALED;
    }

    GLenum result = GLES.glClientWaitSync(sync, flags, timeout);
    LOG_D("glClientWaitSync -> 0x%x", result)

    // Valid return values per spec:
    //   GL_ALREADY_SIGNALED    0x911A
    //   GL_TIMEOUT_EXPIRED     0x911B
    //   GL_CONDITION_SATISFIED 0x911C
    //   GL_WAIT_FAILED         0x911D
    //
    // 0 is not a valid return value.  Some GLES drivers return it when the
    // sync object became invalid between creation and the wait (e.g. a context
    // switch).  Treat it – and GL_WAIT_FAILED – as GL_ALREADY_SIGNALED so the
    // GPU-buffer ring keeps moving rather than hard-crashing the game.
    if (result == 0 || result == GL_WAIT_FAILED) {
        LOG_W("glClientWaitSync: unexpected result 0x%x – treating as GL_ALREADY_SIGNALED", result)
        GLES.glGetError(); // consume any pending error
        return GL_ALREADY_SIGNALED;
    }

    return result;
}

// ---------------------------------------------------------------------------
// glWaitSync  (GPU-side wait – fire and forget)
// ---------------------------------------------------------------------------

extern "C" GLAPI GLAPIENTRY void glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
    LOG()
    LOG_D("glWaitSync, sync: %p, flags: 0x%x", (void*)sync, flags)

    if (is_fake_sync(sync)) {
        LOG_D("glWaitSync: fake sync – no-op")
        return;
    }
    GLES.glWaitSync(sync, flags, timeout);
}

// ---------------------------------------------------------------------------
// glDeleteSync
// ---------------------------------------------------------------------------

extern "C" GLAPI GLAPIENTRY void glDeleteSync(GLsync sync) {
    LOG()
    LOG_D("glDeleteSync, sync: %p", (void*)sync)

    if (is_fake_sync(sync)) {
        LOG_D("glDeleteSync: fake sync – no-op")
        return;
    }
    GLES.glDeleteSync(sync);
}

// ---------------------------------------------------------------------------
// glIsSync
// ---------------------------------------------------------------------------

extern "C" GLAPI GLAPIENTRY GLboolean glIsSync(GLsync sync) {
    LOG()
    LOG_D("glIsSync, sync: %p", (void*)sync)

    if (is_fake_sync(sync)) {
        return GL_TRUE;
    }
    return GLES.glIsSync(sync);
}

// ---------------------------------------------------------------------------
// glGetSynciv
// ---------------------------------------------------------------------------

extern "C" GLAPI GLAPIENTRY void glGetSynciv(GLsync sync, GLenum pname, GLsizei bufSize,
                                              GLsizei* length, GLint* values) {
    LOG()
    LOG_D("glGetSynciv, sync: %p, pname: 0x%x", (void*)sync, pname)

    if (is_fake_sync(sync)) {
        // Fake sync: report it as already signaled for every query.
        if (values && bufSize >= 1) {
            switch (pname) {
            case GL_SYNC_STATUS:
                values[0] = GL_SIGNALED;
                break;
            case GL_SYNC_CONDITION:
                values[0] = GL_SYNC_GPU_COMMANDS_COMPLETE;
                break;
            case GL_SYNC_FLAGS:
                values[0] = 0;
                break;
            default:
                values[0] = 0;
                break;
            }
            if (length) *length = 1;
        }
        return;
    }
    GLES.glGetSynciv(sync, pname, bufSize, length, values);
}

// ---------------------------------------------------------------------------
// ARB aliases (non-Apple platforms only – the macro already handles Apple)
// ---------------------------------------------------------------------------
// On Android the NATIVE_FUNCTION_HEAD macro auto-creates glFenceSyncARB etc.
// as aliases.  Since we're defining our own functions we need to do the same
// manually for completeness.
#ifndef __APPLE__
extern "C" GLAPI GLAPIENTRY GLsync glFenceSyncARB(GLenum condition, GLbitfield flags)
    __attribute__((alias("glFenceSync")));
extern "C" GLAPI GLAPIENTRY GLenum glClientWaitSyncARB(GLsync sync, GLbitfield flags, GLuint64 timeout)
    __attribute__((alias("glClientWaitSync")));
extern "C" GLAPI GLAPIENTRY void glWaitSyncARB(GLsync sync, GLbitfield flags, GLuint64 timeout)
    __attribute__((alias("glWaitSync")));
extern "C" GLAPI GLAPIENTRY void glDeleteSyncARB(GLsync sync)
    __attribute__((alias("glDeleteSync")));
extern "C" GLAPI GLAPIENTRY GLboolean glIsSyncARB(GLsync sync)
    __attribute__((alias("glIsSync")));
extern "C" GLAPI GLAPIENTRY void glGetSyncivARB(GLsync sync, GLenum pname, GLsizei bufSize,
                                                 GLsizei* length, GLint* values)
    __attribute__((alias("glGetSynciv")));
#endif // !__APPLE__
