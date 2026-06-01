// MobileGlues - config/gpu_utils.h
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

#ifndef MOBILEGLUES_PLUGIN_GPU_UTILS_H
#define MOBILEGLUES_PLUGIN_GPU_UTILS_H

#include <string.h>
#include <string>

std::string getGPUInfo();

#ifdef __cplusplus
extern "C"
{
#endif

    int isAdreno(const char* gpu);

    int isAdreno730(const char* gpu);

    int isAdreno740(const char* gpu);

    int isAdreno830(const char* gpu);

    int hasVulkan12();

    bool checkIfANGLESupported(const char* gpu);

#ifdef __APPLE__
    // Apple GPU detection helpers (iOS / macOS)
    // Returns 1 if the renderer string indicates an Apple GPU
    int isAppleGPU(const char* gpu);

    // Returns 1 if this is the Apple A13 GPU (iPhone 11 / 11 Pro / SE 2020)
    // The Metal renderer string reports "Apple A13 GPU"
    int isAppleA13GPU(const char* gpu);

    // Returns a rough GPU tier for Apple Silicon:
    //   0 = unknown / non-Apple
    //   1 = A7âA11  (older, GLES 3.0 / Metal 2)
    //   2 = A12âA14 (current mid-range, GLES 3.2, Metal 3 feature-set)  â iPhone 11 = A13
    //   3 = A15+    (high-end, full compute, large GLSL cache worthwhile)
    int appleGPUTier(const char* gpu);
#endif // __APPLE__

#ifdef __cplusplus
}
#endif

#endif // MOBILEGLUES_PLUGIN_GPU_UTILS_H
