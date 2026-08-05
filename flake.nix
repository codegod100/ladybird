{
  description = "Ladybird browser fork with Nix packaging (OpenBao passkeys/passwords, Haswell hasvk)";

  nixConfig = {
    extra-substituters = [ "https://codegod100.cachix.org" ];
    extra-trusted-public-keys = [
      "codegod100.cachix.org-1:LZFL5VrR644WUjleS3bLbVeOdzlXqzKznQWvD5MVthA="
    ];
  };

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "aarch64-darwin"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
      lib = nixpkgs.lib;

      # Build from this checkout (overlays/patches are already applied in-tree).
      ladybirdSrc = lib.cleanSourceWith {
        src = ./.;
        filter =
          path: type:
          let
            rel = lib.removePrefix (toString ./. + "/") (toString path);
          in
          lib.cleanSourceFilter path type
          && rel != "flake.nix"
          && rel != "flake.lock"
          && !(lib.hasPrefix "nix/" rel)
          && !(lib.hasPrefix "scripts/" rel)
          && !(lib.hasPrefix "outputs/" rel)
          && !(lib.hasPrefix "result" rel);
      };
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = self.packages.${system}.ladybird;
        }
        // pkgs.lib.optionalAttrs (pkgs.lib.hasSuffix "-linux" system) {
          skia = pkgs.callPackage ./nix/packages/skia { };
          ladybird = pkgs.callPackage ./nix/packages/ladybird {
            src = ladybirdSrc;
            inherit (self.packages.${system}) skia;
          };
        }
        // pkgs.lib.optionalAttrs (system == "aarch64-darwin") {
          ladybird = pkgs.callPackage ./nix/packages/ladybird {
            src = ladybirdSrc;
          };
        }
      );

      apps = forAllSystems (
        system:
        {
          default = self.apps.${system}.ladybird;
        }
        // nixpkgs.lib.optionalAttrs (
          builtins.elem system [
            "x86_64-linux"
            "aarch64-linux"
            "aarch64-darwin"
          ]
        ) {
          ladybird = {
            type = "app";
            program = "${self.packages.${system}.ladybird}/bin/Ladybird";
          };
        }
      );
    };
}
