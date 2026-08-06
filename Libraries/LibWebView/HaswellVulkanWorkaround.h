/*
 * Copyright (c) 2026, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWebView/Export.h>

namespace WebView {

// Returns true when the machine has an Intel Haswell (HD Graphics 4xxx) GPU.
WEBVIEW_API bool system_has_intel_haswell_gpu();

// Returns true when VK_ICD_FILENAMES / VK_DRIVER_FILES points at Mesa's hasvk ICD.
WEBVIEW_API bool haswell_hasvk_icd_is_configured();

// Legacy helper: pin Mesa hasvk when present. Prefer apply_haswell_gpu_workarounds() —
// incomplete hasvk still hangs Qt/Wayland and Skia Vulkan probes on Haswell.
WEBVIEW_API bool configure_intel_haswell_vulkan_icd_if_needed();

// On Haswell: force CPU painting, disable Vulkan ICD discovery (fail-fast), and push Qt onto
// software OpenGL. Call from platform_init before QGuiApplication is constructed.
// Returns true when Haswell workarounds were applied.
WEBVIEW_API bool apply_haswell_gpu_workarounds();

// Returns true when Haswell is present (and applies GPU workarounds as a side effect).
WEBVIEW_API bool should_force_cpu_painting_for_haswell_gpu();

}
