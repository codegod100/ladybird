{
  lib,
  stdenv,
  fetchFromGitHub,
  expat,
  fontconfig,
  freetype,
  harfbuzzFull,
  icu,
  gn,
  libGL,
  libjpeg,
  libwebp,
  libx11,
  ninja,
  python3,
  vulkan-headers,
  vulkan-memory-allocator,
  xcbuild,
  cctools,
  zlib,
  fixDarwinDylibNames,

  enableVulkan ? !stdenv.hostPlatform.isDarwin,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "skia";
  # Chrome m148 branch tip matching microsoft/vcpkg skia 148.
  version = "148-unstable-2026-04-14";

  src = fetchFromGitHub {
    owner = "google";
    repo = "skia";
    rev = "e7c90ecca9444fe09598f1630ab7cee2c0ee027a";
    hash = "sha256-2+fxWqkNBStoN6l5Y3xMqkwvh69sCU6A//wz8fMnmjY=";
  };

  postPatch = ''
    substituteInPlace BUILD.gn \
      --replace-fail 'rebase_path("//bin/gn")' '"gn"'
    # System zlib detection bug workaround
    substituteInPlace BUILD.gn \
      --replace-fail '"//third_party/zlib",' ""
  '';

  strictDeps = true;
  nativeBuildInputs = [
    gn
    ninja
    python3
  ]
  ++ lib.optionals stdenv.hostPlatform.isDarwin [
    xcbuild
    cctools.libtool
    zlib
    fixDarwinDylibNames
  ];

  buildInputs = [
    expat
    fontconfig
    freetype
    harfbuzzFull
    icu
    libGL
    libjpeg
    libwebp
    libx11
  ]
  ++ lib.optionals enableVulkan [
    vulkan-headers
    vulkan-memory-allocator
  ];

  gnFlags =
    let
      cpu =
        {
          "x86_64" = "x64";
          "i686" = "x86";
          "arm" = "arm";
          "aarch64" = "arm64";
        }
        .${stdenv.hostPlatform.parsed.cpu.name};
    in
    [
      "is_official_build=true"
      "is_component_build=true"
      "skia_use_dng_sdk=false"
      "skia_use_wuffs=false"
      "extra_cflags=[\"-I${harfbuzzFull.dev}/include/harfbuzz\"]"
      # https://github.com/LadybirdBrowser/ladybird/commit/af3d46dc06829dad65309306be5ea6fbc6a587ec
      "extra_cflags+=[\"-DSKCMS_API=[[gnu::visibility(\\\"default\\\")]]\"]"
      "cc=\"${stdenv.cc.targetPrefix}cc\""
      "cxx=\"${stdenv.cc.targetPrefix}c++\""
      "ar=\"${stdenv.cc.targetPrefix}ar\""
      "target_cpu=\"${cpu}\""
    ]
    ++ map (lib: "skia_use_system_${lib}=true") [
      "zlib"
      "harfbuzz"
      "libpng"
      "libwebp"
    ]
    ++ lib.optionals enableVulkan [
      "skia_use_vulkan=true"
    ]
    ++ lib.optionals stdenv.hostPlatform.isDarwin [
      "skia_use_fontconfig=true"
      "skia_use_freetype=true"
      "skia_use_metal=true"
    ];

  env.NIX_LDFLAGS = lib.optionalString stdenv.hostPlatform.isDarwin "-lz";

  installPhase = ''
    runHook preInstall

    mkdir -p $out/lib
    cp *.so *.a *.dylib $out/lib

    pushd ../../include
    find . -name '*.h' -exec install -Dm644 {} $out/include/skia/{} \;
    popd
    pushd ../../modules
    find . -name '*.h' -exec install -Dm644 {} $out/include/skia/modules/{} \;
    popd

    mkdir -p $out/lib/pkgconfig
    cat > $out/lib/pkgconfig/skia.pc <<'EOF'
    prefix=${placeholder "out"}
    exec_prefix=''${prefix}
    libdir=''${prefix}/lib
    includedir=''${prefix}/include/skia
    Name: skia
    Description: 2D graphic library for drawing text, geometries and images.
    URL: https://skia.org/
    Version: ${lib.versions.major finalAttrs.version}
    Libs: -L''${libdir} -lskia
    Cflags: -I''${includedir}
    EOF

    runHook postInstall
  '';

  preFixup = ''
    for file in $(grep -rl '#include "include/' $out/include); do
      substituteInPlace "$file" \
        --replace-fail '#include "include/' '#include "'
    done
  '';

  meta = {
    description = "2D graphic library for drawing text, geometries and images (Chrome m148)";
    homepage = "https://skia.org/";
    license = lib.licenses.bsd3;
    platforms = with lib.platforms;
      arm ++ aarch64 ++ x86 ++ x86_64;
    pkgConfigModules = [ "skia" ];
  };
})
