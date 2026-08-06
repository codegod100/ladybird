#!/bin/sh
# Haswell (HD Graphics 4xxx) GPU workaround for the Ladybird UI process.
# @ladybird@ is substituted at install time.
#
# Incomplete Mesa Vulkan + Qt Wayland EGL hang the UI on this GPU (even with CPU painting).
# Force: disabled Vulkan ICD, software GL, and QT_QPA_PLATFORM=xcb (XWayland).
# Set LADYBIRD_ALLOW_WAYLAND=1 to keep native Wayland (unsupported on Haswell).
#
# POSIX sh only: do not use bash `exec -a`.

_haswell_gpu() {
  for _card in /sys/class/drm/card*; do
    case "$_card" in
      *-*) continue ;;
    esac
    [ -d "$_card/device" ] || continue

    if [ -r "$_card/device/vendor" ] && [ -r "$_card/device/device" ]; then
      _vendor=$(tr -d '\n' < "$_card/device/vendor" | tr 'A-F' 'a-f')
      _device=$(tr -d '\n' < "$_card/device/device" | tr 'A-F' 'a-f')
      case "$_vendor" in
        0x8086|8086)
          case "$_device" in
            0x0402|0x0406|0x040a|0x040b|0x040e|0x0412|0x0416|0x041a|0x041b|0x041e|\
            0x0a02|0x0a06|0x0a0a|0x0a0b|0x0a0e|0x0a12|0x0a16|0x0a1a|0x0a1b|0x0a1e|\
            0x0a22|0x0a26|0x0a2a|0x0a2b|0x0a2e|0x0d02|0x0d06|0x0d0a|0x0d0b|0x0d0e|\
            0x0d12|0x0d16|0x0d1a|0x0d1b|0x0d1e|0x0d22|0x0d26|0x0d2a|0x0d2b|0x0d2e|\
            0402|0406|040a|040b|040e|0412|0416|041a|041b|041e|\
            0a02|0a06|0a0a|0a0b|0a0e|0a12|0a16|0a1a|0a1b|0a1e|\
            0a22|0a26|0a2a|0a2b|0a2e|0d02|0d06|0d0a|0d0b|0d0e|\
            0d12|0d16|0d1a|0d1b|0d1e|0d22|0d26|0d2a|0d2b|0d2e)
              return 0
              ;;
          esac
          ;;
      esac
    fi

    if [ -r "$_card/device/uevent" ] \
      && grep -qiE 'PCI_ID=8086:(0402|0406|040A|040B|040E|0412|0416|041A|041B|041E|0A02|0A06|0A0A|0A0B|0A0E|0A12|0A16|0A1A|0A1B|0A1E|0A22|0A26|0A2A|0A2B|0A2E|0D02|0D06|0D0A|0D0B|0D0E|0D12|0D16|0D1A|0D1B|0D1E|0D22|0D26|0D2A|0D2B|0D2E)' "$_card/device/uevent"; then
      return 0
    fi
  done
  return 1
}

if _haswell_gpu; then
  # Always overwrite — session env may still pin hasvk from older wrappers.
  export VK_ICD_FILENAMES=/var/empty/ladybird-disabled-vulkan-icd.json
  export VK_DRIVER_FILES=/var/empty/ladybird-disabled-vulkan-icd.json
  export QT_OPENGL=software
  export LIBGL_ALWAYS_SOFTWARE=1
  if [ -z "${LADYBIRD_ALLOW_WAYLAND:-}" ]; then
    export QT_QPA_PLATFORM=xcb
  fi
fi

exec "@ladybird@" "$@"
