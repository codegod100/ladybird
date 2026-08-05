#!/bin/sh
# Pin Mesa hasvk on Intel Haswell when Vulkan ICD is unset.
# @ladybird@ is substituted at install time to the Qt-wrapped binary.
#
# POSIX sh only: do not use bash `exec -a` (dash /bin/sh rejects it with
# "exec: -a: not found"). argv0 from the wrapper path is not required for
# Ladybird; forwarding to the Qt-wrapped binary is enough.
if [ -z "${VK_ICD_FILENAMES:-}" ] && [ -z "${VK_DRIVER_FILES:-}" ]; then
  _hasvk="/run/opengl-driver/share/vulkan/icd.d/intel_hasvk_icd.x86_64.json"
  if [ -r "$_hasvk" ]; then
    for _uevent in /sys/class/drm/card*/device/uevent; do
      [ -r "$_uevent" ] || continue
      # Mesa hasvk Gen7.5 (Haswell) PCI IDs.
      if grep -qE 'PCI_ID=8086:(0402|0406|040A|040B|040E|0412|0416|041A|041B|041E|0A02|0A06|0A0A|0A0B|0A0E|0A12|0A16|0A1A|0A1B|0A1E|0A22|0A26|0A2A|0A2B|0A2E|0D02|0D06|0D0A|0D0B|0D0E|0D12|0D16|0D1A|0D1B|0D1E|0D22|0D26|0D2A|0D2B|0D2E)' "$_uevent"; then
        export VK_ICD_FILENAMES="$_hasvk"
        export VK_DRIVER_FILES="$_hasvk"
        break
      fi
    done
  fi
fi
exec "@ladybird@" "$@"
