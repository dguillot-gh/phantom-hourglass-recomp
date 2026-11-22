{
  description = "Phantom Hourglass Recomp - native Linux build (you supply your own ROM)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.listToAttrs (map (system: {
        name = system;
        value = f (import nixpkgs { inherit system; });
      }) systems);

      # The PC build fetches SDL3 release-3.4.18 at configure time. Pin it here too so that
      # `nix build` is reproducible and works in the Nix sandbox (no network during the build).
      sdl3Src = pkgs: pkgs.fetchFromGitHub {
        owner = "libsdl-org";
        repo = "SDL";
        rev = "release-3.4.18";
        hash = "sha256-3dP4FJ5OvZI6UPykJpE3hTH6kdW+Nn7gUSf3yiPCobo=";
      };

      # Same commit as .gitmodules / `git submodule status` in this repository.
      melondsSrc = pkgs: pkgs.fetchFromGitHub {
        owner = "melonDS-emu";
        repo = "melonDS";
        rev = "906e9ebb27da8c6a715cd7abab4abfe8a8d29427";
        hash = "sha256-hJFzH7qPk2NIXl9OQQyqSId6uofJ//FoslmbyX5ZjUk=";
      };

      # Headers/libs SDL3's CMake configuration needs on Linux (X11, Wayland, audio, ...).
      sdlDeps = pkgs: pkgs.sdl3.buildInputs ++ [ pkgs.wayland-scanner ];

      # SDL also dlopen()s drivers at runtime; keep these where the executable can find them.
      runtimeLibs = pkgs: sdlDeps pkgs ++ [ pkgs.mesa pkgs.libglvnd pkgs.libjack2 ];

      buildTools = pkgs: with pkgs; [
        cmake
        ninja
        gcc
        pkg-config
        git
        python3
      ];

      # GCC's wrapper knows where libstdc++ is; nixpkgs' clang-scan-deps does not when CMake uses
      # clang for a C++20 Ninja build, so use GCC for the Nix builds.
      cmakeFlagsBase = pkgs:
        "-DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ " +
        "-DFETCHCONTENT_SOURCE_DIR_SDL3=${sdl3Src pkgs} -DMELONDS_DIR=${melondsSrc pkgs}";
    in {
      packages = forAllSystems (pkgs: rec {
        # Program, runtime and unit tests without a ROM. Pure build: `nix build .#program`.
        program = pkgs.stdenv.mkDerivation {
          pname = "phantom-hourglass-program";
          version = "0.1.0";
          src = self;
          nativeBuildInputs = buildTools pkgs ++ sdlDeps pkgs;
          buildInputs = runtimeLibs pkgs;

          configurePhase = ''
            runHook preConfigure
            cmake -S . -B build -G Ninja ${cmakeFlagsBase pkgs} -DFFCC_BUILD_TESTS=ON
            runHook postConfigure
          '';
          buildPhase = ''
            runHook preBuild
            cmake --build build --parallel $NIX_BUILD_CORES
            runHook postBuild
          '';
          doCheck = true;
          checkPhase = ''
            runHook preCheck
            ctest --test-dir build --output-on-failure
            runHook postCheck
          '';
          installPhase = ''
            runHook preInstall
            mkdir -p $out/bin
            cp build/frontend/PhantomHourglass $out/bin/
            runHook postInstall
          '';
          meta = with pkgs.lib; {
            description = "Phantom Hourglass Recomp host program (build with a ROM to play)";
            platforms = [ "x86_64-linux" "aarch64-linux" ];
          };
        };

        # Full game: the ROM is recompiled from the user's own dump. Nix cannot copy an
        # untracked file into the store in a pure evaluation, so this derivation is impure:
        #   PH_ROM=/roms/your-dump.nds nix build .#game --impure
        game = let
          romEnv = builtins.getEnv "PH_ROM";
          rom = if romEnv == ""
            then throw "Set PH_ROM to your Phantom Hourglass dump: PH_ROM=/roms/your-dump.nds nix build .#game --impure"
            else builtins.path { path = romEnv; name = "phantom-hourglass.nds"; };
          romName = builtins.baseNameOf rom;
        in pkgs.stdenv.mkDerivation {
          pname = "phantom-hourglass";
          version = "0.1.0";
          src = self;

          nativeBuildInputs = buildTools pkgs ++ sdlDeps pkgs;
          buildInputs = runtimeLibs pkgs;

          configurePhase = ''
            runHook preConfigure
            cmake -S . -B build -G Ninja ${cmakeFlagsBase pkgs} -DFFCC_BUILD_TESTS=OFF -DPH_ROM=${rom}
            runHook postConfigure
          '';
          buildPhase = ''
            runHook preBuild
            cmake --build build --parallel $NIX_BUILD_CORES
            runHook postBuild
          '';
          installPhase = ''
            runHook preInstall
            mkdir -p $out/bin $out/share/phantom-hourglass/rom
            cp build/frontend/PhantomHourglass $out/bin/
            cp build/frontend/libph_game.so $out/share/phantom-hourglass/
            cp ${rom} $out/share/phantom-hourglass/rom/${romName}
            cat > $out/bin/play-phantom-hourglass <<EOF
            #!/bin/sh
            exec $out/bin/PhantomHourglass --data-dir "\$HOME/.local/share/phantom-hourglass" \
              "$out/share/phantom-hourglass/rom/${romName}" "\$@"
            EOF
            chmod +x $out/bin/play-phantom-hourglass
            runHook postInstall
          '';
          meta = with pkgs.lib; {
            description = "Phantom Hourglass Recomp (compiled from your own ROM)";
            platforms = [ "x86_64-linux" "aarch64-linux" ];
          };
        };

        default = game;
      });

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          packages = buildTools pkgs ++ sdlDeps pkgs ++ [ pkgs.xvfb-run pkgs.mesa ];
          shellHook = ''
            export CC=gcc CXX=g++
            # Use the pinned sources instead of `git submodule update` / a network fetch at configure time.
            export FETCHCONTENT_SOURCE_DIR_SDL3="${sdl3Src pkgs}"
            export MELONDS_DIR="${melondsSrc pkgs}"
            # SDL3 and the other shared libraries need to be found when running the built program.
            export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath (runtimeLibs pkgs)}''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
            echo "Phantom Hourglass Recomp: nix develop ready."
            echo "Build with: ./scripts/build-linux.sh --rom /roms/your-dump.nds"
          '';
        };
      });
    };
}
