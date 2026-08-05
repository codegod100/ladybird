{
  lib,
  stdenv,
  src,
  fetchFromGitHub,
  unicode-emoji,
  unicode-character-database,
  unicode-idna,
  publicsuffix-list,
  chromium-hsts-preload-list,
  cmake,
  ninja,
  pkg-config,
  curlFull,
  libavif,
  angle,
  libjxl,
  libedit,
  libpulseaudio,
  libwebp,
  libxcrypt,
  mimalloc,
  openssl,
  perl,
  python3,
  qt6Packages,
  makeShellWrapper,
  woff2,
  cargo,
  cpptrace,
  fast-float,
  ffmpeg,
  fmt,
  fontconfig,
  harfbuzz,
  rustPlatform,
  rustc,
  simdutf,
  skia,
  libtommath,
  sdl3,
  icu78,
  simdjson,
  libxml2,
  sqlite,
  glslang,
  vulkan-headers,
  vulkan-loader,
  vulkan-memory-allocator,
  wrapGAppsHook3,
  gtk3,
  gsettings-desktop-schemas,
  mesa,
}:

let
  # Ladybird master pins mimalloc 2.2.7 (API differs from nixpkgs 3.x).
  mimallocLadybird = mimalloc.overrideAttrs (_: {
    version = "2.2.7";
    src = fetchFromGitHub {
      owner = "microsoft";
      repo = "mimalloc";
      rev = "v2.2.7";
      hash = "sha256-z9qMOTcGkURblZChXDGfQ58hrql52lG6EE1NQmxxuj0=";
    };
  });

  # Ladybird expects wuffs v0.3 single-file headers (not nixpkgs wuffs 0.4).
  wuffsMirror = fetchFromGitHub {
    owner = "google";
    repo = "wuffs-mirror-release-c";
    rev = "v0.3.4";
    hash = "sha256-V7inWJqH7Q4Ac/ZB//7XHrpgfAYUPBxWBerBem6Q/Kk=";
  };
in

stdenv.mkDerivation (finalAttrs: {
  pname = "ladybird";
  version = "0-unstable-2026-08-03";

  inherit src;

  cargoDeps = rustPlatform.fetchCargoVendor {
    inherit (finalAttrs) src;
    hash = "sha256-2asgV8IT3QKXvPezmP7VP+idLGDR/jfUa38/mErm7VI=";
  };

  postPatch = ''
    sed -i '/iconutil/d' UI/CMakeLists.txt

    perl -0pi -e \
      's/find_package\(ICU 78\.[0-9]+ EXACT REQUIRED COMPONENTS data i18n uc\)/find_package(ICU ${icu78.version} EXACT REQUIRED COMPONENTS data i18n uc)/ or die "ICU dependency not found\n"' \
      Meta/CMake/check_for_dependencies.cmake

    substituteInPlace Meta/CMake/lagom_install_options.cmake \
      --replace-fail "\''${CMAKE_INSTALL_BINDIR}" "bin" \
      --replace-fail "\''${CMAKE_INSTALL_LIBDIR}" "lib"
  '';

  preConfigure = ''
    mkdir -p .wuffs-include/wuffs
    cp ${wuffsMirror}/release/c/wuffs-v0.3.c .wuffs-include/wuffs/
    export CMAKE_INCLUDE_PATH="$PWD/.wuffs-include''${CMAKE_INCLUDE_PATH:+:}$CMAKE_INCLUDE_PATH"

    mkdir -p build/Caches

    cp -r ${unicode-character-database}/share/unicode build/Caches/UCD
    chmod +w build/Caches/UCD
    cp ${unicode-emoji}/share/unicode/emoji/emoji-test.txt build/Caches/UCD
    cp ${unicode-idna}/share/unicode/idna/IdnaMappingTable.txt build/Caches/UCD
    echo -n ${unicode-character-database.version} > build/Caches/UCD/version.txt
    chmod -w build/Caches/UCD

    mkdir build/Caches/PublicSuffix
    cp ${publicsuffix-list}/share/publicsuffix/public_suffix_list.dat build/Caches/PublicSuffix

    mkdir build/Caches/HSTSPreload
    cp ${chromium-hsts-preload-list}/share/chromium-hsts-preload-list/transport_security_state_static.json build/Caches/HSTSPreload
  '';

  nativeBuildInputs = [
    cargo
    cmake
    ninja
    perl
    pkg-config
    python3
    rustPlatform.cargoSetupHook
    rustc
    # qtWrapperArgs uses --run (Haswell hasvk); binary wrappers reject that.
    (qt6Packages.wrapQtAppsHook.override { makeBinaryWrapper = makeShellWrapper; })
    libtommath
  ]
  ++ lib.optionals stdenv.hostPlatform.isLinux [
    glslang
    # Qt may open GTK file dialogs; schemas must land on XDG_DATA_DIRS.
    wrapGAppsHook3
  ];

  # Let wrapQtAppsHook own wrapping; merge GLib/GTK schema env into it.
  dontWrapGApps = true;

  buildInputs = [
    curlFull
    cpptrace
    fast-float
    ffmpeg
    fmt
    fontconfig
    harfbuzz
    libavif
    angle
    libjxl
    libedit
    libwebp
    libxcrypt
    libxml2
    mimallocLadybird
    openssl
    qt6Packages.qtbase
    qt6Packages.qtmultimedia
    sdl3
    simdutf
    skia
    sqlite
    woff2
    icu78
    simdjson
  ]
  ++ lib.optionals stdenv.hostPlatform.isLinux [
    libpulseaudio.dev
    qt6Packages.qtwayland
    vulkan-headers
    vulkan-loader
    vulkan-memory-allocator
    gtk3
    gsettings-desktop-schemas
  ];

  preFixup = lib.optionalString stdenv.hostPlatform.isLinux ''
    qtWrapperArgs+=("''${gappsWrapperArgs[@]}")
  '';

  cmakeFlags = [
    (lib.cmakeBool "ENABLE_LTO_FOR_RELEASE" false)
    "-DLADYBIRD_CACHE_DIR=Caches"
    "-DENABLE_NETWORK_DOWNLOADS=OFF"
    (lib.cmakeFeature "ICU_ROOT" (toString icu78.dev))
  ]
  ++ lib.optionals stdenv.hostPlatform.isLinux [
    "-DCMAKE_INSTALL_LIBEXECDIR=libexec"
  ];

  env.NIX_LDFLAGS = "-lGL -lfontconfig";
  # Vendored wuffs-v0.3.c trips newer GCC diagnostics that Ladybird enables as
  # -Werror (suggest-override, calloc-transposed-args). Keep them non-fatal.
  env.NIX_CFLAGS_COMPILE = toString [
    "-Wno-error=suggest-override"
    "-Wno-error=calloc-transposed-args"
  ];

  postInstall = lib.optionalString stdenv.hostPlatform.isDarwin ''
    mkdir -p $out/Applications $out/bin
    mv $out/bundle/Ladybird.app $out/Applications
  '';

  dontWrapQtApps = stdenv.hostPlatform.isDarwin;

  # Keep qtWrapperArgs as a real bash array so the --run script is not
  # word-split (required for multiline --run with wrapQtAppsHook).
  __structuredAttrs = true;

  # Haswell (Intel HD 4xxx) only works via Mesa hasvk. With every ICD present,
  # Ladybird's Compositor vkCreateInstance returns VK_ERROR_INCOMPATIBLE_DRIVER
  # (-9) and the process dies instead of falling back. Pin hasvk on those GPUs
  # unless the user already set VK_ICD_FILENAMES / VK_DRIVER_FILES.
  postFixup = lib.optionalString stdenv.hostPlatform.isLinux ''
    if [ -x "$out/bin/Ladybird" ]; then
      mv "$out/bin/Ladybird" "$out/bin/.Ladybird-qtwrapped"
      substitute ${./hasvk-wrapper.sh} "$out/bin/Ladybird" \
        --subst-var-by ladybird "$out/bin/.Ladybird-qtwrapped" \
        --subst-var-by hasvk_icd "${mesa}/share/vulkan/icd.d/intel_hasvk_icd.x86_64.json"
      chmod +x "$out/bin/Ladybird"
    fi
  '';

  meta = {
    description = "Browser using the SerenityOS LibWeb engine with a Qt or Cocoa GUI";
    homepage = "https://github.com/codegod100/ladybird";
    changelog = "https://github.com/codegod100/ladybird/commits/master";
    license = lib.licenses.bsd2;
    mainProgram = "Ladybird";
    platforms = [
      "x86_64-linux"
      "aarch64-linux"
      "aarch64-darwin"
    ];
    broken = stdenv.hostPlatform.isDarwin;
  };
})
