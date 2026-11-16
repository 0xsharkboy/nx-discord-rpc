{
  description = "nx-discord-rpc: Discord Rich Presence sysmodule for Nintendo Switch";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    devkitNix.url = "github:bandithedoge/devkitNix";
  };

  outputs = { self, nixpkgs, flake-utils, devkitNix }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          overlays = [ devkitNix.overlays.default ];
        };
      in
      {
        devShells.default = pkgs.mkShell.override { stdenv = pkgs.devkitNix.stdenvA64; } {
          buildInputs = [ pkgs.cmake pkgs.gnumake pkgs.gcc pkgs.bear pkgs.clang-tools ];
          shellHook = ''
            export PATH="$DEVKITPRO/devkitA64/bin:$PATH"
          '';
        };
      });
}
