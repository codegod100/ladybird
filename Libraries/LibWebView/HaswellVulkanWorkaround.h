/*
 * Copyright (c) 2026, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWebView/Export.h>

namespace WebView {

// Intel Haswell (Gen 7.5) only supports Vulkan via Mesa's hasvk driver. When every ICD is
// visible, vkCreateInstance often returns VK_ERROR_INCOMPATIBLE_DRIVER and GPU presentation
// never comes up. Pin hasvk when we detect those GPUs, replacing a non-hasvk ICD if needed.
WEBVIEW_API bool configure_intel_haswell_vulkan_icd_if_needed();

// Returns true when the machine has an Intel Haswell (HD Graphics 4xxx) GPU.
WEBVIEW_API bool system_has_intel_haswell_gpu();

// Returns true when VK_ICD_FILENAMES / VK_DRIVER_FILES points at Mesa's hasvk ICD.
WEBVIEW_API bool haswell_hasvk_icd_is_configured();

// Returns true when Haswell is present but hasvk is not configured and CPU painting should be used.
WEBVIEW_API bool should_force_cpu_painting_for_haswell_gpu();

}
