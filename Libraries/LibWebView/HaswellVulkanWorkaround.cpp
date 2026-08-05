/*
 * Copyright (c) 2026, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Array.h>
#include <AK/LexicalPath.h>
#include <AK/Platform.h>
#include <LibCore/Directory.h>
#include <LibCore/Environment.h>
#include <LibCore/File.h>
#include <LibFileSystem/FileSystem.h>
#include <LibWebView/HaswellVulkanWorkaround.h>

#if defined(AK_OS_LINUX) && !defined(AK_OS_ANDROID)

namespace {

static constexpr StringView hasvk_icd_filename = "intel_hasvk_icd.x86_64.json"sv;

// Mesa hasvk Gen7.5 (Haswell) PCI device IDs (8086:xxxx).
static constexpr Array<StringView, 40> haswell_pci_device_ids = {
    "0402"sv, "0406"sv, "040a"sv, "040b"sv, "040e"sv, "0412"sv, "0416"sv, "041a"sv, "041b"sv, "041e"sv,
    "0a02"sv, "0a06"sv, "0a0a"sv, "0a0b"sv, "0a0e"sv, "0a12"sv, "0a16"sv, "0a1a"sv, "0a1b"sv, "0a1e"sv,
    "0a22"sv, "0a26"sv, "0a2a"sv, "0a2b"sv, "0a2e"sv, "0d02"sv, "0d06"sv, "0d0a"sv, "0d0b"sv, "0d0e"sv,
    "0d12"sv, "0d16"sv, "0d1a"sv, "0d1b"sv, "0d1e"sv, "0d22"sv, "0d26"sv, "0d2a"sv, "0d2b"sv, "0d2e"sv,
};

static StringView strip_pci_id_prefix(StringView value)
{
    auto normalized = value;
    if (normalized.starts_with("0x"sv, CaseSensitivity::CaseInsensitive))
        normalized = normalized.substring_view(2);

    while (normalized.length() > 4 && normalized[0] == '0')
        normalized = normalized.substring_view(1);

    return normalized;
}

static bool is_haswell_pci_device_id(StringView device_id)
{
    auto normalized = strip_pci_id_prefix(device_id);
    for (auto id : haswell_pci_device_ids) {
        if (normalized.equals_ignoring_ascii_case(id))
            return true;
    }
    return false;
}

static bool is_intel_vendor_id(StringView vendor_id)
{
    return strip_pci_id_prefix(vendor_id).equals_ignoring_ascii_case("8086"sv);
}

static bool icd_path_points_at_hasvk(StringView icd_path)
{
    for (auto component : icd_path.split_view(':')) {
        if (component.is_empty())
            continue;
        if (component.contains("hasvk"sv) && FileSystem::exists(component))
            return true;
    }
    return false;
}

static Optional<ByteString> hasvk_icd_path_in_directory(StringView directory)
{
    auto icd_path = LexicalPath(directory).append("vulkan/icd.d"sv).append(hasvk_icd_filename).string();
    if (FileSystem::exists(icd_path))
        return icd_path;
    return {};
}

static Optional<ByteString> find_hasvk_icd_path()
{
    if (auto icd_path = Core::Environment::get("LADYBIRD_HASVK_ICD"sv); icd_path.has_value() && FileSystem::exists(*icd_path))
        return icd_path->to_byte_string();

    static constexpr Array<StringView, 4> candidate_paths = {
        "/run/opengl-driver/share/vulkan/icd.d/intel_hasvk_icd.x86_64.json"sv,
        "/usr/share/vulkan/icd.d/intel_hasvk_icd.x86_64.json"sv,
        "/usr/lib/x86_64-linux-gnu/GL/vulkan/icd.d/intel_hasvk_icd.x86_64.json"sv,
        "/usr/lib64/vulkan/icd.d/intel_hasvk_icd.x86_64.json"sv,
    };

    for (auto path : candidate_paths) {
        if (FileSystem::exists(path))
            return path.to_byte_string();
    }

    if (auto xdg_data_dirs = Core::Environment::get("XDG_DATA_DIRS"sv); xdg_data_dirs.has_value()) {
        for (auto directory : xdg_data_dirs->split_view(':')) {
            if (auto icd_path = hasvk_icd_path_in_directory(directory); icd_path.has_value())
                return icd_path;
        }
    }

    if (auto icd_path = hasvk_icd_path_in_directory("/usr/share"sv); icd_path.has_value())
        return icd_path;

    return {};
}

static Optional<ByteString> read_trimmed_sysfs_value(StringView path)
{
    auto file_or_error = Core::File::open(path, Core::File::OpenMode::Read);
    if (file_or_error.is_error())
        return {};

    auto contents = file_or_error.release_value()->read_until_eof();
    if (contents.is_error())
        return {};

    auto trimmed = StringView { contents.value() }.trim_whitespace(TrimMode::Both);
    if (trimmed.is_empty())
        return {};

    return trimmed.to_byte_string();
}

static bool drm_device_is_intel_haswell(StringView card_name)
{
    auto device_path = ByteString::formatted("/sys/class/drm/{}/device/device", card_name);
    auto vendor_path = ByteString::formatted("/sys/class/drm/{}/device/vendor", card_name);

    auto device_id = read_trimmed_sysfs_value(device_path);
    auto vendor_id = read_trimmed_sysfs_value(vendor_path);
    if (device_id.has_value() && vendor_id.has_value())
        return is_intel_vendor_id(*vendor_id) && is_haswell_pci_device_id(*device_id);

    auto uevent_path = ByteString::formatted("/sys/class/drm/{}/device/uevent", card_name);
    auto uevent_or_error = Core::File::open(uevent_path, Core::File::OpenMode::Read);
    if (uevent_or_error.is_error())
        return false;

    auto uevent = uevent_or_error.release_value()->read_until_eof();
    if (uevent.is_error())
        return false;

    for (auto line : StringView { uevent.value() }.lines()) {
        if (!line.starts_with("PCI_ID="sv))
            continue;
        auto pci_id = line.substring_view("PCI_ID="sv.length());
        auto colon_index = pci_id.find(':');
        if (!colon_index.has_value())
            continue;
        auto vendor = pci_id.substring_view(0, colon_index.value());
        auto device = pci_id.substring_view(colon_index.value() + 1);
        if (is_intel_vendor_id(vendor) && is_haswell_pci_device_id(device))
            return true;
    }

    return false;
}

}

namespace WebView {

bool system_has_intel_haswell_gpu()
{
    bool found_haswell = false;
    auto flags = static_cast<Core::DirIterator::Flags>(Core::DirIterator::SkipDots | Core::DirIterator::NoStat);
    auto result = Core::Directory::for_each_entry("/sys/class/drm"sv, flags, [&](Core::DirectoryEntry const& entry, Core::Directory const&) -> ErrorOr<IterationDecision> {
        if (found_haswell || !entry.name.starts_with("card"sv))
            return IterationDecision::Continue;

        // Skip connector entries such as card0-HDMI-A-1; only cardN refers to the GPU device.
        if (entry.name.contains('-'))
            return IterationDecision::Continue;

        if (drm_device_is_intel_haswell(entry.name)) {
            found_haswell = true;
            return IterationDecision::Break;
        }

        return IterationDecision::Continue;
    });

    return !result.is_error() && found_haswell;
}

bool haswell_hasvk_icd_is_configured()
{
    for (auto variable : Array { "VK_ICD_FILENAMES"sv, "VK_DRIVER_FILES"sv }) {
        if (auto value = Core::Environment::get(variable); value.has_value() && icd_path_points_at_hasvk(*value))
            return true;
    }
    return false;
}

bool configure_intel_haswell_vulkan_icd_if_needed()
{
    if (!system_has_intel_haswell_gpu())
        return false;

    if (haswell_hasvk_icd_is_configured())
        return true;

    auto icd_path = find_hasvk_icd_path();
    if (!icd_path.has_value())
        return false;

    // Haswell only works with Mesa hasvk. Replace a non-hasvk ICD (e.g. iris/anv) so accidental
    // Vulkan probes do not load an incompatible driver.
    (void)Core::Environment::set("VK_ICD_FILENAMES"sv, icd_path.value().view(), Core::Environment::Overwrite::Yes);
    (void)Core::Environment::set("VK_DRIVER_FILES"sv, icd_path.value().view(), Core::Environment::Overwrite::Yes);
    return true;
}

bool should_force_cpu_painting_for_haswell_gpu()
{
    if (!system_has_intel_haswell_gpu())
        return false;

    // Always prefer CPU painting on Haswell. Mesa hasvk is incomplete; probing either anv or hasvk
    // from the Qt UI / compositor can hang and leave an unresponsive native window capturing input.
    // Still pin hasvk when available so any accidental Vulkan probe uses the least-bad ICD.
    (void)configure_intel_haswell_vulkan_icd_if_needed();
    return true;
}

}

#else

namespace WebView {

bool configure_intel_haswell_vulkan_icd_if_needed()
{
    return false;
}

bool system_has_intel_haswell_gpu()
{
    return false;
}

bool haswell_hasvk_icd_is_configured()
{
    return false;
}

bool should_force_cpu_painting_for_haswell_gpu()
{
    return false;
}

}

#endif
