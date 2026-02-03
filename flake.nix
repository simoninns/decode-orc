{
  description = "decode-orc - LaserDisc and tape decoding orchestration framework";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    qtnodes = {
      url = "github:paceholder/nodeeditor";
      flake = false;
    };
    encode-orc = {
      url = "github:simoninns/encode-orc";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { self, nixpkgs, flake-utils, qtnodes, encode-orc }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config = {
            allowUnfree = true; # In case any dependencies require it
          };
        };

        # Helper to get version from git or use a default
        version = if (builtins.pathExists ./.git) then
          builtins.readFile (pkgs.runCommand "get-version" {} ''
            cd ${./.}
            ${pkgs.git}/bin/git describe --tags --always --dirty 2>/dev/null || echo "1.0.15" > $out
          '')
        else
          "1.0.15";

        # Build QtNodes as a separate package
        qtNodes = pkgs.stdenv.mkDerivation {
          pname = "qtnodes";
          version = "3.0.0";

          src = qtnodes;

          nativeBuildInputs = with pkgs; [
            cmake
            qt6.wrapQtAppsHook
          ];

          buildInputs = with pkgs; [
            qt6.qtbase
          ];

          cmakeFlags = [
            "-DUSE_QT6=ON"
            "-DBUILD_TESTING=OFF"
            "-DBUILD_EXAMPLES=OFF"
            "-DBUILD_SHARED_LIBS=OFF"
          ];

          # Define NODE_EDITOR_STATIC for static linking
          CXXFLAGS = "-DNODE_EDITOR_STATIC";

          meta = with pkgs.lib; {
            description = "Qt-based library for node graph editing";
            homepage = "https://github.com/paceholder/nodeeditor";
            license = licenses.bsd3;
          };
        };

        # Build the decode-orc package
        decode-orc = pkgs.stdenv.mkDerivation {
          pname = "decode-orc";
          version = builtins.replaceStrings ["\n"] [""] version;

          src = self;

          nativeBuildInputs = with pkgs; [
            cmake
            pkg-config
            git
            qt6.wrapQtAppsHook
          ];

          buildInputs = with pkgs; [
            # Core dependencies from vcpkg.json
            spdlog
            sqlite
            yaml-cpp
            libpng
            fftw

            # FFmpeg components
            ffmpeg

            # Qt6 for GUI
            qt6.qtbase
            qt6.qttools

            # QtNodes built from flake input
            qtNodes
          ];

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
            "-DBUILD_TESTS=ON"
            "-DBUILD_GUI=ON"
            "-DBUILD_DOCS=OFF"
            "-DBUILD_ENCODE_ORC=ON"
            # Tell CMake where to find QtNodes
            "-DQtNodes_DIR=${qtNodes}/lib/cmake/QtNodes"
          ];

          # Ensure git is available for version detection and patch shebangs
          preConfigure = ''
            # If we're in a git repo, let CMake detect the version
            if [ -d .git ]; then
              export GIT_EXECUTABLE=${pkgs.git}/bin/git
            fi
            
            # Copy encode-orc submodule into expected location
            mkdir -p external/encode-orc
            cp -r ${encode-orc}/* external/encode-orc/
            chmod -R u+w external/encode-orc
            
            # Patch shell script shebangs to use Nix bash
            patchShebangs check_mvp_architecture.sh
            patchShebangs encode-tests.sh || true
          '';

          # Make ffprobe available during tests
          preCheck = ''
            export PATH=${pkgs.ffmpeg}/bin:$PATH
          '';

          meta = with pkgs.lib; {
            description = "Cross-platform orchestration and processing framework for LaserDisc and tape decoding workflows";
            homepage = "https://github.com/simoninns/decode-orc";
            license = licenses.gpl3Plus;
            platforms = platforms.linux ++ platforms.darwin;
            maintainers = [ ];
          };
        };

      in
      {
        # The package that can be built with `nix build`
        packages = {
          default = decode-orc;
          decode-orc = decode-orc;
        };

        # Development shell with all dependencies
        devShells.default = pkgs.mkShell {
          inputsFrom = [ decode-orc ];

          buildInputs = with pkgs; [
            # Additional development tools
            cmake-format
            clang-tools
            gdb
            valgrind
            ccache
            doxygen
            graphviz

            # Version control
            git

            # Build tools already included from decode-orc buildInputs
          ];

          shellHook = ''
            echo "🎬 decode-orc development environment"
            echo "======================================"
            echo ""
            echo "Available commands:"
            echo "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug"
            echo "  cmake --build build -j$(nproc)"
            echo "  ./build/bin/orc-gui"
            echo "  ./build/bin/orc-cli"
            echo ""
            echo "CMake version: $(cmake --version | head -n1)"
            echo "Qt version: ${pkgs.qt6.qtbase.version}"
            echo ""
            
            # Set up ccache if available
            export CMAKE_CXX_COMPILER_LAUNCHER=ccache
            
            # Ensure build directory exists
            mkdir -p build
          '';

          # Environment variables
          CMAKE_EXPORT_COMPILE_COMMANDS = 1;
          QT_QPA_PLATFORM = "xcb"; # For Linux
        };

        # Alternative minimal shell without GUI dependencies
        devShells.minimal = pkgs.mkShell {
          buildInputs = with pkgs; [
            cmake
            pkg-config
            git
            
            # Core dependencies only (no Qt)
            spdlog
            sqlite
            yaml-cpp
            libpng
            fftw
            ffmpeg
          ];

          cmakeFlags = [
            "-DBUILD_GUI=OFF"
            "-DBUILD_TESTS=ON"
          ];

          shellHook = ''
            echo "🎬 decode-orc minimal development environment (CLI only)"
            echo "========================================================"
            echo "GUI components disabled"
          '';
        };

        # Apps that can be run with `nix run`
        apps.default = {
          type = "app";
          program = "${decode-orc}/bin/orc-gui";
        };

        apps.orc-gui = {
          type = "app";
          program = "${decode-orc}/bin/orc-gui";
        };

        apps.orc-cli = {
          type = "app";
          program = "${decode-orc}/bin/orc-cli";
        };
      }
    );
}
