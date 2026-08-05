/*
 * Copyright (c) 2026, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Platform.h>
#include <LibCore/Directory.h>
#include <LibCore/Environment.h>
#include <LibCore/File.h>
#include <LibFileSystem/FileSystem.h>
#include <LibWebView/HaswellVulkanWorkaround.h>

#if defined(AK_OS_LINUX) && !defined(AK_OS_ANDROID)

namespace {

// Mesa hasvk Gen7.5 (Haswell) PCI device IDs (8086:xxxx).
static constexpr Array<StringView, 48> haswell_pci_device_ids = {
    "0402"sv, "0406"sv, "040A"sv, "040B"sv, "040E"sv, "0412"sv, "0416"sv, "041A"sv, "041B"sv, "041E"sv,
    "0A02"sv, "0A06"sv, "0A0A"sv, "0A0B"sv, "0A0E"sv, "0A12"sv, "0A16"sv, "0A1A"sv, "0A1B"sv, "0A1E"sv,
    "0A22"sv, "0A26"sv, "0A2A"sv, "0A2B"sv, "0A2E"sv, "0D02"sv, "0D06"sv, "0D0A"sv, "0D0B"sv, "0D0E"sv,
    "0D12"sv, "0D16"sv, "0D1A"sv, "0D1B"sv, "0D1E"sv, "0D22"sv, "0D26"sv, "0D2A"sv, "0D2B"sv, "0D2E"sv,
};

static bool is_haswell_pci_id(StringView device_id)
{
    return haswell_pci_device_ids.contains_slow(device_id);
}

static bool system_has_intel_haswell_gpu()
{
    bool found_haswell = false;
    auto flags = static_cast<Core::DirIterator::Flags>(Core::DirIterator::SkipDots | Core::DirIterator::NoStat);
    auto result = Core::Directory::for_each_entry("/sys/class/drm"sv, flags, [&](Core::DirectoryEntry const& entry, Core::Directory const&) -> ErrorOr<IterationDecision> {
        if (found_haswell || !entry.name.starts_with("card"sv))
            return IterationDecision::Continue;

        auto uevent_path = ByteString::formatted("/sys/class/drm/{}/device/uevent", entry.name);
        auto uevent_or_error = Core::File::open(uevent_path, Core::File::OpenMode::Read);
        if (uevent_or_error.is_error())
            return IterationDecision::Continue;

        auto uevent = uevent_or_error.release_value()->read_until_eof();
        if (uevent.is_error())
            return IterationDecision::Continue;

        auto uevent_view = StringView { uevent.value() };
        for (auto line : uevent_view.lines()) {
            if (!line.starts_with("PCI_ID="sv))
                continue;
            auto pci_id = line.substring_view("PCI_ID="sv.length());
            if (!pci_id.starts_with("8086:"sv))
                continue;
            if (is_haswell_pci_id(pci_id.substring_view(5))) {
                found_haswell = true;
                break;
            }
        }

        return IterationDecision::Continue;
    });

    return !result.is_error() && found_haswell;
}

static Optional<ByteString> find_hasvk_icd_path()
{
    static constexpr Array<StringView, 2> candidate_paths = {
        "/run/opengl-driver/share/vulkan/icd.d/intel_hasvk_icd.x86_64.json"sv,
        "/usr/share/vulkan/icd.d/intel_hasvk_icd.x86_64.json"sv,
    };

    for (auto path : candidate_paths) {
        if (FileSystem::exists(path))
            return path.to_byte_string();
    }

    return {};
}

}

namespace WebView {

void configure_intel_haswell_vulkan_icd_if_needed()
{
    if (Core::Environment::has("VK_ICD_FILENAMES"sv) || Core::Environment::has("VK_DRIVER_FILES"sv))
        return;

    if (!system_has_intel_haswell_gpu())
        return;

    auto icd_path = find_hasvk_icd_path();
    if (!icd_path.has_value())
        return;

    (void)Core::Environment::set("VK_ICD_FILENAMES"sv, icd_path.value(), Core::Environment::Overwrite::Yes);
    (void)Core::Environment::set("VK_DRIVER_FILES"sv, icd_path.value(), Core::Environment::Overwrite::Yes);
}

}

#else

namespace WebView {

void configure_intel_haswell_vulkan_icd_if_needed()
{
}

}

#endif
