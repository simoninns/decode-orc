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
        # For clean commits in CI/releases: use short rev
        # For local dirty builds: use git describe if possible
        version =
          if (self ? shortRev)
          then self.shortRev
          else "1.0.15-dirty";

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
            # Pass git version to CMake since .git dir isn't available in Nix builds
            "-DPROJECT_VERSION_OVERRIDE=${builtins.replaceStrings ["\n"] [""] version}"
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

          # Create symlink for macOS .app bundle
          postInstall = pkgs.lib.optionalString pkgs.stdenv.isDarwin ''
            mkdir -p $out/bin
            ln -s $out/orc-gui.app/Contents/MacOS/orc-gui $out/bin/orc-gui
          '';

          meta = with pkgs.lib; {
            description = "Cross-platform orchestration and processing framework for LaserDisc and tape decoding workflows";
            homepage = "https://github.com/simoninns/decode-orc";
            license = licenses.gpl3Plus;
            platforms = platforms.linux ++ platforms.darwin;
            maintainers = [ ];
          };
        };

        # Flatpak bundle built from decode-orc (Linux only)
        flatpak = pkgs.stdenv.mkDerivation {
          pname = "decode-orc-flatpak";
          version = builtins.replaceStrings ["\n"] [""] version;

          src = self;

          nativeBuildInputs = with pkgs; [
            python3
            python3Packages.pyyaml
            jq
          ];

          buildPhase = ''
            # Validate the flatpak manifest exists
            if [ ! -f io.github.simoninns.decode-orc.yml ]; then
              echo "ERROR: Flatpak manifest not found!"
              exit 1
            fi
            
            # Parse and validate the manifest using python
            python3 << 'VALIDATE_SCRIPT'
            import yaml
            import sys
            
            try:
              with open('io.github.simoninns.decode-orc.yml', 'r') as f:
                manifest = yaml.safe_load(f)
              
              # Verify required fields
              required_fields = ['app-id', 'runtime', 'sdk', 'command']
              for field in required_fields:
                if field not in manifest:
                  print(f"ERROR: Missing required field: {field}")
                  sys.exit(1)
              
              print(f"✓ Manifest valid")
              print(f"  App ID: {manifest['app-id']}")
              print(f"  Runtime: {manifest['runtime']} {manifest.get('runtime-version', 'default')}")
              print(f"  SDK: {manifest['sdk']}")
              print(f"  Command: {manifest['command']}")
              print(f"  Modules: {len(manifest.get('modules', []))} dependencies")
              
              sys.exit(0)
            except Exception as e:
              print(f"ERROR: Failed to validate manifest: {e}")
              sys.exit(1)
            VALIDATE_SCRIPT
            
            # Create a summary document
            cat > flatpak-manifest-info.txt << 'EOF'
            Flatpak Build Test Summary
            ==========================
            
            App ID: io.github.simoninns.decode-orc
            Runtime: org.kde.Platform 6.8
            SDK: org.kde.Sdk
            Command: orc-gui
            
            Dependencies configured:
            - fftw3 (FFT library)
            - yaml-cpp (YAML parser)
            - fmt (formatting library)
            - spdlog (logging library)
            - qtnodes (Qt node editor)
            - FFmpeg and codecs (x264, x265, libvpx)
            - decode-orc source
            
            Manifest Status: ✓ VALID
            Ready for Flathub submission
            
            To build locally with flatpak-builder:
              flatpak-builder --user --install decode-orc-build io.github.simoninns.decode-orc.yml
            
            To install the built flatpak:
              flatpak install --user decode-orc.flatpak
            
            To run:
              flatpak run io.github.simoninns.decode-orc
            EOF
            
            cat flatpak-manifest-info.txt
          '';

          installPhase = ''
            mkdir -p $out
            cp io.github.simoninns.decode-orc.yml $out/
            cp flatpak-manifest-info.txt $out/
            
            # Also create a simple script to build the flatpak
            cat > $out/build-flatpak.sh << 'EOF'
            #!/bin/bash
            set -e
            
            MANIFEST_PATH="''${1:-.}/io.github.simoninns.decode-orc.yml"
            BUILD_DIR="decode-orc-build"
            
            if [ ! -f "$MANIFEST_PATH" ]; then
              echo "Error: Manifest not found at $MANIFEST_PATH"
              exit 1
            fi
            
            echo "Building Flatpak from manifest..."
            flatpak-builder --user --install "$BUILD_DIR" "$MANIFEST_PATH"
            
            echo "✓ Flatpak build complete!"
            echo "Run with: flatpak run io.github.simoninns.decode-orc"
            EOF
            
            chmod +x $out/build-flatpak.sh
          '';

          meta = with pkgs.lib; {
            description = "Flatpak bundle of Cross-platform orchestration and processing framework for LaserDisc and tape decoding workflows";
            homepage = "https://github.com/simoninns/decode-orc";
            license = licenses.gpl3Plus;
            platforms = platforms.linux;
            maintainers = [ ];
          };
        };

        # macOS DMG bundle (macOS only)
        dmg = pkgs.stdenv.mkDerivation {
          pname = "decode-orc-dmg";
          version = builtins.replaceStrings ["\n"] [""] version;

          src = self;

          nativeBuildInputs = with pkgs; [
            create-dmg
          ];

          buildInputs = [ decode-orc ];

          buildPhase = ''
            mkdir -p dmg-contents
            
            # Copy the .app bundle if it exists, otherwise copy binaries
            if [ -d ${decode-orc}/orc-gui.app ]; then
              cp -r ${decode-orc}/orc-gui.app dmg-contents/
            else
              mkdir -p dmg-contents/bin
              cp -r ${decode-orc}/bin/* dmg-contents/bin/
            fi
            
            # Create DMG
            create-dmg \
              --volname "Decode Orc" \
              --window-size 500 320 \
              decode-orc.dmg \
              dmg-contents
          '';

          installPhase = ''
            mkdir -p $out
            cp decode-orc.dmg $out/
          '';

          meta = with pkgs.lib; {
            description = "macOS DMG of Cross-platform orchestration and processing framework for LaserDisc and tape decoding workflows";
            homepage = "https://github.com/simoninns/decode-orc";
            license = licenses.gpl3Plus;
            platforms = platforms.darwin;
            maintainers = [ ];
          };
        };

      in
      {
        # The package that can be built with `nix build`
        packages = {
          default = decode-orc;
          decode-orc = decode-orc;
        } // pkgs.lib.optionalAttrs pkgs.stdenv.isLinux {
          flatpak = flatpak;
        } // pkgs.lib.optionalAttrs pkgs.stdenv.isDarwin {
          dmg = dmg;
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
