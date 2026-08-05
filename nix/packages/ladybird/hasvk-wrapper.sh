#!/bin/sh
# Pin Mesa hasvk on Intel Haswell when Vulkan ICD is unset or points at a non-hasvk driver.
# Also force CPU painting + xcb so opening tabs / typing cannot hang on Wayland+Vulkan.
# @ladybird@ and @hasvk_icd@ are substituted at install time.
#
# POSIX sh only: do not use bash `exec -a` (dash /bin/sh rejects it with
# "exec: -a: not found"). argv0 from the wrapper path is not required for
# Ladybird; forwarding to the Qt-wrapped binary is enough.

_haswell_gpu() {
  for _card in /sys/class/drm/card*; do
    # Skip connector entries such as card0-HDMI-A-1.
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

# True only when every non-empty ICD entry points at hasvk (mixed lists still need pinning).
_icd_list_is_hasvk_only() {
  _list=$1
  _saw_entry=0
  _old_ifs=$IFS
  IFS=:
  for _entry in $_list; do
    [ -n "$_entry" ] || continue
    _saw_entry=1
    case "$_entry" in
      *hasvk*) ;;
      *)
        IFS=$_old_ifs
        return 1
        ;;
    esac
  done
  IFS=$_old_ifs
  [ "$_saw_entry" -eq 1 ]
}

_should_pin_hasvk() {
  if ! _haswell_gpu; then
    return 1
  fi
  if [ -n "${VK_ICD_FILENAMES:-}" ] && _icd_list_is_hasvk_only "$VK_ICD_FILENAMES"; then
    return 1
  fi
  if [ -n "${VK_DRIVER_FILES:-}" ] && _icd_list_is_hasvk_only "$VK_DRIVER_FILES"; then
    return 1
  fi
  return 0
}

_args_contain_force_cpu_painting() {
  for _arg in "$@"; do
    case "$_arg" in
      --force-cpu-painting) return 0 ;;
    esac
  done
  return 1
}

if _haswell_gpu; then
  # Opening a new tab still created a Qt Vulkan surface on older builds; force the CPU path
  # from the wrapper so even a stale binary / missed C++ detection cannot hang on typing.
  export LADYBIRD_FORCE_CPU_PAINTING=1

  # Wayland text-input + incomplete Haswell Vulkan feels "locked" when typing in a new tab.
  if [ -z "${QT_QPA_PLATFORM:-}" ]; then
    export QT_QPA_PLATFORM=xcb
    echo "Intel Haswell GPU detected; preferring QT_QPA_PLATFORM=xcb over Wayland" >&2
  fi

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

  if ! _args_contain_force_cpu_painting "$@"; then
    echo "Intel Haswell GPU detected; enabling --force-cpu-painting" >&2
    exec "@ladybird@" --force-cpu-painting "$@"
  fi
fi

exec "@ladybird@" "$@"
