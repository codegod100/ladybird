#!/bin/sh
# Pin Mesa hasvk on Intel Haswell when Vulkan ICD is unset or points at a non-hasvk driver.
# @ladybird@ and @hasvk_icd@ are substituted at install time.
#
# POSIX sh only: do not use bash `exec -a` (dash /bin/sh rejects it with
# "exec: -a: not found"). argv0 from the wrapper path is not required for
# Ladybird; forwarding to the Qt-wrapped binary is enough.

_haswell_gpu() {
  for _card in /sys/class/drm/card[0-9]*; do
    [ -d "$_card/device" ] || continue
    if [ -r "$_card/device/vendor" ] && [ -r "$_card/device/device" ]; then
      _vendor=$(tr -d '\n' < "$_card/device/vendor")
      _device=$(tr -d '\n' < "$_card/device/device")
      case "$_vendor" in
        0x8086|8086)
          case "$_device" in
            0x0402|0x0406|0x040A|0x040B|0x040E|0x0412|0x0416|0x041A|0x041B|0x041E|\
            0x0A02|0x0A06|0x0A0A|0x0A0B|0x0A0E|0x0A12|0x0A16|0x0A1A|0x0A1B|0x0A1E|\
            0x0A22|0x0A26|0x0A2A|0x0A2B|0x0A2E|0x0D02|0x0D06|0x0D0A|0x0D0B|0x0D0E|\
            0x0D12|0x0D16|0x0D1A|0x0D1B|0x0D1E|0x0D22|0x0D26|0x0D2A|0x0D2B|0x0D2E|\
            0402|0406|040A|040B|040E|0412|0416|041A|041B|041E|\
            0A02|0A06|0A0A|0A0B|0A0E|0A12|0A16|0A1A|0A1B|0A1E|\
            0A22|0A26|0A2A|0A2B|0A2E|0D02|0D06|0D0A|0D0B|0D0E|\
            0D12|0D16|0D1A|0D1B|0D1E|0D22|0D26|0D2A|0D2B|0D2E)
              return 0
            esac
          ;;
        esac
      fi
    fi
    if grep -qE 'PCI_ID=8086:(0402|0406|040A|040B|040E|0412|0416|041A|041B|041E|0A02|0A06|0A0A|0A0B|0A0E|0A12|0A16|0A1A|0A1B|0A1E|0A22|0A26|0A2A|0A2B|0A2E|0D02|0D06|0D0A|0D0B|0D0E|0D12|0D16|0D1A|0D1B|0D1E|0D22|0D26|0D2A|0D2B|0D2E)' "$_card/device/uevent" 2>/dev/null; then
      return 0
    fi
  done
  return 1
}

_icd_points_at_hasvk() {
  case "$1" in
    *hasvk*) return 0 ;;
  esac
  return 1
}

_should_pin_hasvk() {
  if ! _haswell_gpu; then
    return 1
  fi
  if [ -n "${VK_ICD_FILENAMES:-}" ] && _icd_points_at_hasvk "$VK_ICD_FILENAMES"; then
    return 1
  fi
  if [ -n "${VK_DRIVER_FILES:-}" ] && _icd_points_at_hasvk "$VK_DRIVER_FILES"; then
    return 1
  fi
  return 0
}

if _should_pin_hasvk; then
  _hasvk=""
  if [ -r "@hasvk_icd@" ]; then
    _hasvk="@hasvk_icd@"
  elif [ -r "/run/opengl-driver/share/vulkan/icd.d/intel_hasvk_icd.x86_64.json" ]; then
    _hasvk="/run/opengl-driver/share/vulkan/icd.d/intel_hasvk_icd.x86_64.json"
  fi

  if [ -n "$_hasvk" ]; then
    export LADYBIRD_HASVK_ICD="$_hasvk"
    export VK_ICD_FILENAMES="$_hasvk"
    export VK_DRIVER_FILES="$_hasvk"
  fi
fi

exec "@ladybird@" "$@"
