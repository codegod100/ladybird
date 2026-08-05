/*
 * Copyright (c) 2026, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

namespace WebView {

// Intel Haswell (Gen 7.5) only supports Vulkan via Mesa's hasvk driver. When every ICD is
// visible, vkCreateInstance often returns VK_ERROR_INCOMPATIBLE_DRIVER and GPU presentation
// never comes up. Pin hasvk when we detect those GPUs unless the user already chose an ICD.
void configure_intel_haswell_vulkan_icd_if_needed();

}
