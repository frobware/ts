# shell.nix
let
  flake = builtins.getFlake (toString ./.);
in
  flake.outputs.devShells.${builtins.currentSystem}.default
