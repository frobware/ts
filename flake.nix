{
  description = "A Nix flake for the ts project.";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  };

  outputs = { self, nixpkgs, ... }:
  let
    forAllSystems = function: nixpkgs.lib.genAttrs [ "aarch64-darwin" "aarch64-linux" "x86_64-darwin" "x86_64-linux" ] (
      system: function system nixpkgs.legacyPackages.${system}
    );

    overlay = final: prev: {
      ts = final.callPackage ./package.nix {};
    };
  in {
    overlays.default = overlay;

    packages = forAllSystems (system: pkgs: {
      default = pkgs.callPackage ./package.nix {};
    });

    devShells = forAllSystems (system: pkgs: {
      default = pkgs.mkShell {
        buildInputs = [
          self.packages.${system}.default.buildInputs
        ];
        nativeBuildInputs = [
          pkgs.hyperfine
          pkgs.include-what-you-use
          pkgs.pkg-config
        ]
        ++ pkgs.lib.optional (!pkgs.stdenv.hostPlatform.isDarwin) pkgs.gdb
        ++ pkgs.lib.optional (!pkgs.stdenv.hostPlatform.isDarwin) pkgs.ltrace
        ++ pkgs.lib.optional (!pkgs.stdenv.hostPlatform.isDarwin) pkgs.valgrind;

        shellHook = ''
          remove_from_env() {
            local env_var_name=$1
            shift  # Shift the first argument so we can loop over the rest
            IFS=' ' read -ra env_var_array <<< "''${!env_var_name}"
            for value_to_remove in "$@"; do
              for i in "''${!env_var_array[@]}"; do
                if [[ "''${env_var_array[i]}" == "$value_to_remove" ]]; then
                  unset 'env_var_array[i]'
                fi
              done
            done
            if [ "''${#env_var_array[@]}" -eq 0 ]; then
              unset "$env_var_name"
            else
              printf -v "$env_var_name" '%s ' "''${env_var_array[@]}"
              export "$env_var_name"
            fi
          }

          export ASAN_OPTIONS=abort_on_error=1
          export TS_BUILD_WITH_ASAN=1
          export TS_BUILD_WITH_DEBUG=1

          if [[ "{$TS_BUILD_WITH_DEBUG:-" ]]; then
            # There's a better debug experience when _FORTIFY_SOURCE is not set.
            # Variables stop being reported as 'optimised out'.
            echo "DEBUG build: removing 'fortify fortify3' from \$NIX_HARDENING_ENABLE"
            remove_from_env NIX_HARDENING_ENABLE fortify fortify3
          fi

          ${pkgs.lib.optionalString pkgs.stdenv.hostPlatform.isDarwin ''
            # I simply have more joy debugging with /usr/bin/clang and
            # /usr/bin/lldb on macOS.
            export CC=/usr/bin/clang
            export EXTRA_CFLAGS="''$(pkg-config --cflags libpcre)"
            export EXTRA_LDFLAGS="''$(pkg-config --libs-only-L libpcre)"
          ''}
        '';
      };
    });
  };
}
