{
  description = "Decode-Orc - LaserDisc and tape decoding orchestration framework";

  # Upstream dependencies for the flake
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.11";
    flake-utils.url = "github:numtide/flake-utils";
    qtnodes = {
      url = "github:paceholder/nodeeditor";
      flake = false;
    };
  };

  # Build outputs for each supported system
  outputs = { self, nixpkgs, flake-utils, qtnodes }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        # Import Nixpkgs for this system
        pkgs = import nixpkgs {
          inherit system;
          config = {
            allowUnfree = true; # In case any dependencies require it
          };
        };

        # Helper to get version from git or use a default
        # For clean commits in CI/releases: include branch + short rev
        # For local dirty builds: use a fixed fallback
        branch =
          if (self ? sourceInfo && self.sourceInfo ? ref)
          then self.sourceInfo.ref
          else "detached";

        commit = if (self ? shortRev) then self.shortRev else null;

        rawVersion =
          if (commit != null)
          then "${branch}-${commit}"
          else "0.0.0-dirty";

        version = builtins.replaceStrings ["\n" "/" " "] ["" "-" "-"] rawVersion;

        # Build QtNodes as a separate package (no external package needed)
        qtNodes = pkgs.stdenv.mkDerivation {
          pname = "qtnodes";
          version = "3.0.0";

          src = qtnodes;

          strictDeps = true;

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            qt6.wrapQtAppsHook
          ];

          buildInputs = with pkgs; [
            qt6.qtbase
          ];

          cmakeFlags = [
            "-GNinja"
            "-DUSE_QT6=ON"
            "-DBUILD_TESTING=OFF"
            "-DBUILD_EXAMPLES=OFF"
            "-DBUILD_SHARED_LIBS=OFF"
          ];

          meta = with pkgs.lib; {
            description = "Qt-based library for node graph editing";
            homepage = "https://github.com/paceholder/nodeeditor";
            license = licenses.bsd3;
          };
        };

        # Build the decode-orc package (primary output)
        decode-orc = pkgs.stdenv.mkDerivation {
          pname = "decode-orc";
          version = version;

          src = self;

          strictDeps = true;

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            qt6.wrapQtAppsHook
          ];

          qtWrapperArgs = pkgs.lib.optionals pkgs.stdenv.isDarwin [
            "--set"
            "QT_QPA_PLATFORM"
            "cocoa"
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
            "-GNinja"
            "-DCMAKE_BUILD_TYPE=Release"
            "-DBUILD_TESTS=ON"
            "-DBUILD_GUI=ON"
            "-DBUILD_DOCS=OFF"
            "-DBUILD_ENCODE_ORC=OFF"
            # Tell CMake where to find QtNodes
            "-DQtNodes_DIR=${qtNodes}/lib/cmake/QtNodes"
            # Pass git version to CMake since .git dir isn't available in Nix builds
            "-DPROJECT_VERSION_OVERRIDE=${version}"
            # Define NODE_EDITOR_STATIC to match QtNodes static build
            "-DCMAKE_CXX_FLAGS=-DNODE_EDITOR_STATIC"
          ];

          # Patch scripts for Nix sandbox compatibility
          postPatch = ''
            # Patch shell script shebangs to use Nix bash
            patchShebangs cmake/check_mvp_architecture.sh
            patchShebangs encode-tests.sh || true
          '';

          # Make ffprobe available during tests
          preCheck = ''
            export PATH=${pkgs.ffmpeg}/bin:$PATH
          '';

          doInstallCheck = true;

          installCheckPhase = ''
            ctest --output-on-failure -R MVPArchitectureCheck
          '';

          # Create symlink for macOS .app bundle
          postInstall = pkgs.lib.optionalString pkgs.stdenv.isDarwin ''
            mkdir -p $out/bin
            ln -s $out/orc-gui.app/Contents/MacOS/orc-gui $out/bin/orc-gui
          '';

          meta = with pkgs.lib; {
            description = "Decode-Orc - LaserDisc and tape decoding orchestration framework";
            homepage = "https://github.com/simoninns/decode-orc";
            license = licenses.gpl3Plus;
            platforms = platforms.linux ++ platforms.darwin;
            maintainers = [ ];
          };
        };

      in
      {
        # Packages that can be built with `nix build`
        packages = {
          default = decode-orc;
          decode-orc = decode-orc;
        };

        # Apps that can be run with `nix run`
        apps = {
          default = {
            type = "app";
            program = "${decode-orc}/bin/orc-gui";
          };
          orc-gui = {
            type = "app";
            program = "${decode-orc}/bin/orc-gui";
          };
          orc-cli = {
            type = "app";
            program = "${decode-orc}/bin/orc-cli";
          };
        };

        # Development shell with all dependencies for `nix develop`
        devShells.default = pkgs.mkShell {
          inputsFrom = [ decode-orc ];

          packages = with pkgs; [
            # Additional development tools
            cmake-format
            clang-tools
            ccache
            doxygen
            graphviz
            ninja

            # Version control
            git
          ] ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
            gdb
            valgrind
          ] ++ pkgs.lib.optionals pkgs.stdenv.isDarwin [
            lldb
          ];

          shellHook = ''
            echo "decode-orc nix development environment"
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
          QT_QPA_PLATFORM = pkgs.lib.optionalString pkgs.stdenv.isLinux "xcb"; # For Linux
        };

      }
    );
}
