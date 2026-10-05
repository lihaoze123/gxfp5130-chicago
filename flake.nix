{
  description = "Native GXFP5130 ChicagoHS support for NixOS";
  inputs.nixpkgs.url = "git+https://github.com/NixOS/nixpkgs.git?ref=nixpkgs-unstable";
  outputs = { self, nixpkgs }: let
    system = "x86_64-linux";
    pkgs = import nixpkgs { inherit system; };
    packages = import ./nix/packages.nix { inherit pkgs; };
  in {
    packages.${system} = packages // { default = packages.fprintd; };
    nixosModules.default = import ./nix/module.nix;
    checks.${system} = { inherit (packages) matcher-tests; };
  };
}
