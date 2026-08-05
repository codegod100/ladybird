# Nix packaging

Build this Ladybird fork with Nix. Passkeys/password-manager changes and the
deer.social scroll fix are already applied in-tree (not overlays).

```bash
nix build                 # .#ladybird
nix build .#skia          # Chrome m148 Skia (linux)
nix run                   # launch Ladybird

# Incremental develop builds
./scripts/ladybird-devshell-build.sh
```

Binary cache: [`codegod100`](https://app.cachix.org/cache/codegod100) (via `nixConfig`).

CI: `.github/workflows/nix.yml` builds on nixbuild.net and pushes to Cachix.
