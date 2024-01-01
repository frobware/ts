# TS (timestamp standard input)

This utility, a C reimplementation of the `ts` command from the
[`moreutils`](https://joeyh.name/code/moreutils/) package, adds
timestamps to lines of standard input.

While this version eliminates the dependency on Perl and its
associated packages, it requires the [PCRE](https://www.pcre.org/)
library for regular expression support, both at build and runtime.

Please note that the repository does not currently offer pre-compiled
binaries, requiring users to compile the utility from source.

## Building & Installation

Before compiling the `ts` utility, you must have the
[PCRE](https://www.pcre.org/) development library installed on your
system. The development library is commonly named `pcre-devel`,
`libpcre3-dev`, or a similar variant, depending on your operating
system and distribution.

```bash
git clone https://github.com/frobware/ts
cd ts
make INSTALL_BINDIR=$HOME/.local/bin install
```

## Nix/NixOS Support

This utility can be easily integrated into NixOS configurations or
consumed in Nix environments using Nix Flakes. Below is an example of
how to include the `ts` utility in your NixOS system or project.

### Adding the ts Nix Flake

To use the `ts` utility in your NixOS configuration or any Nix project,
first, include the flake in your flake.nix file:

```nix
{
  description = "Example Nix flake for integrating the `ts` utility into a NixOS configuration.";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    ts-flake.url = "https://github.com/frobware/ts";
  };

  outputs = { self, nixpkgs, ts-flake, ... }@inputs:
  let
    pkgsWithOverlay = system: import nixpkgs {
      inherit system;
      overlays = [ ts-flake.overlays.default ];
    };

    buildNixOS = { system, modules }: let
      pkgs = pkgsWithOverlay system;
    in nixpkgs.lib.nixosSystem {
      inherit system modules pkgs;
    };
  in
  {
    nixosConfigurations.exampleHost = buildNixOS {
      system = "aarch64-linux";
      modules = [
        ({ pkgs, ... }: {
          environment.systemPackages = [ pkgs.ts ];
        })
      ];
    };
  };
}
```

## Licensing

The entire ts project, including both the source code now in C and the
manpage documentation, is licensed under the GNU General Public
License version 2 (GPLv2). This unified licensing approach ensures
full compliance with the original moreutils package's licensing terms,
from which the manpage documentation is verbatim copied, and aligns
the reimplemented `ts` utility under the same open-source license.
