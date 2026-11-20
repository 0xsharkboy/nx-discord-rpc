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
          buildInputs = [ pkgs.cmake pkgs.gnumake pkgs.gcc pkgs.bear pkgs.clang-tools pkgs.pkg-config pkgs.zip pkgs.zlib ];
          shellHook = ''
            export PATH="$DEVKITPRO/devkitA64/bin:$PATH"
          '';
        };

        devShells.desktop =
          let
            runtimeLibs = [
              pkgs.libGL
              pkgs.libxkbcommon
              pkgs.wayland
              pkgs.libx11
              pkgs.libxcursor
              pkgs.libxext
              pkgs.libxi
              pkgs.libxinerama
              pkgs.libxrandr
            ];
          in
          pkgs.mkShell {
            nativeBuildInputs = [
              pkgs.cmake
              pkgs.gnumake
              pkgs.pkg-config
              pkgs.wayland-scanner
              pkgs.xvfb-run
              pkgs.xorg.xwd
              pkgs.imagemagick
              pkgs.xdotool
            ];
            buildInputs = runtimeLibs ++ [
              pkgs.dbus
              pkgs.wayland-protocols
              pkgs.libxrender
              pkgs.xorgproto
            ];
            shellHook = ''
              export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath runtimeLibs}''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
            '';
          };
      });
}
