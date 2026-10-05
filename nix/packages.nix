{ pkgs }:
let
  libfprint = pkgs.callPackage ./libfprint.nix {};
  fprintd = pkgs.callPackage ./fprintd.nix { libfprint-gxfp = libfprint; };
  tools = pkgs.stdenvNoCC.mkDerivation {
    pname = "gxfp5130-chicago-tools";
    version = "1";
    src = ../tools;
    nativeBuildInputs = [ pkgs.makeWrapper ];
    nativeCheckInputs = [ pkgs.python3 ];
    doCheck = true;
    checkPhase = ''
      runHook preCheck
      python3 -m unittest discover -s tests -v
      runHook postCheck
    '';
    installPhase = ''
      mkdir -p $out/libexec $out/bin
      cp *.py $out/libexec/
      for name in import enroll verify; do
        makeWrapper ${pkgs.python3}/bin/python3 $out/bin/gxfp-chicago-$name \
          --add-flags "$out/libexec/chicago.py $name" \
          --prefix PATH : ${pkgs.lib.makeBinPath [ fprintd pkgs.systemd pkgs.coreutils ]}
      done
    '';
  };
  matcher-tests = pkgs.stdenv.mkDerivation {
    pname = "gxfp5130-chicago-matcher-tests";
    version = "1";
    src = ../libfprint/libfprint/drivers/gxfpmoc;
    nativeBuildInputs = [ pkgs.cmake pkgs.pkg-config ];
    buildInputs = [ pkgs.mbedtls pkgs.glib ];
    cmakeFlags = [ "-DBUILD_TESTING=ON" ];
    doCheck = true;
    checkPhase = "ctest --output-on-failure";
    installPhase = "mkdir -p $out; cp Testing/Temporary/LastTest.log $out/";
  };
in {
  inherit libfprint fprintd tools matcher-tests;
  kernel = pkgs.callPackage ./kernel.nix { kernel = pkgs.linuxPackages.kernel; };
}
