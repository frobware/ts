{ lib, pkgs, stdenv, ... }:

stdenv.mkDerivation {
  name = "ts";
  src = ./.;

  buildInputs = [ pkgs.pcre ];
  nativeBuildInputs = [ pkgs.installShellFiles ];

  buildPhase = ''
    make clean
    make
  '';

  installPhase = ''
    mkdir -p $out/bin
    make INSTALL_BINDIR=$out/bin install
    mkdir -p $out/share/man/man1
    cp ./share/man/man1/ts.1 $out/share/man/man1/ts.1
    sed -i "s/@VERSION@/1.0/g" $out/share/man/man1/ts.1
    sed -i "s/@DATE@/$(date +%FT%T)/g" $out/share/man/man1/ts.1
    installShellCompletion --zsh --name _ts ./share/zsh/site-functions/_ts
  '';

  meta = {
    description = "Timestamp standard input.";
    license = lib.licenses.gpl2;
    maintainers = [ lib.mkMaintainer { name = "frobware-x"; } ];
  };
}
