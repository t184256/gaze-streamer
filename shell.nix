{ pkgs ? import <nixpkgs> { }, tobii }:
with pkgs;
mkShell {
  buildInputs = [ tobii.tobii-library ];
}
