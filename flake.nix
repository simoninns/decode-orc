{
  description = "Decode-Orc - LaserDisc and tape decoding orchestration framework";

  # Upstream dependencies for the flake
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
    # nixpkgs-unstable is required for ONNX Runtime ≥ 1.23.2 (nn_ntsc_chroma_sink_stage).
    # nixos-25.11 ships 1.22.2 which is insufficient for chroma_net_v2.onnx.
    nixpkgs-unstable.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    qtnodes = {
      url = "github:paceholder/nodeeditor";
      flake = false;
    };
    ezpwd = {
      url = "github:pjkundert/ezpwd-reed-solomon/62a490c13f6e057fbf2dc6777fde234c7a19098e";
      flake = false;
    };
  };

  # Build outputs for each supported system
  outputs = { self, nixpkgs, nixpkgs-unstable, flake-utils, qtnodes, ezpwd }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        # Import Nixpkgs for this system
        pkgs = import nixpkgs {
          inherit system;
          config = {
            allowUnfree = true; # In case any dependencies require it
          };
        };

        # nixpkgs-unstable is used for packages that require a newer version
        # than what nixos-25.11 provides (currently: ONNX Runtime ≥ 1.23.2).
        pkgs-unstable = import nixpkgs-unstable {
          inherit system;
          config = {
            allowUnfree = true;
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

        # On macOS, Nix's default stdenv uses Apple SDK 10.12.2 which pre-dates
        # aligned_alloc (added in macOS 10.15). Override to SDK 11.0 so libc++
        # can find ::aligned_alloc in the global namespace.
        stdenv = if pkgs.stdenv.isDarwin
                 then pkgs.overrideSDK pkgs.stdenv "11.0"
                 else pkgs.stdenv;

        # Filtered ezpwd headers-only derivation (avoids VERSION file collisions)
        ezpwd-headers = pkgs.runCommand "ezpwd-headers" {} ''
          mkdir -p $out
          cp -r ${ezpwd}/c++ $out/ 2>/dev/null || true
          if [ -d "$out/c++/ezpwd" ]; then
            ln -s c++/ezpwd $out/ezpwd
          fi
        '';

        # Build QtNodes as a separate package (no external package needed)
        qtNodes = stdenv.mkDerivation {
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

        # Python environment for MkDocs documentation tooling
        mkdocsPythonEnv = pkgs.python312.withPackages (ps: [
          ps.mkdocs
          ps.mkdocs-material
          ps."mkdocs-awesome-nav"
        ]);

        # Build the decode-orc package (primary output)
        decode-orc = stdenv.mkDerivation {
          pname = "decode-orc";
          version = version;

          src = builtins.path {
            path = ./.;
            name = "decode-orc-docs-src";
          };

          strictDeps = true;

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            qt6.wrapQtAppsHook
          ] ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
            wrapGAppsHook3
          ];

          # Wrap Qt binaries and include gapps runtime settings on Linux.
          dontWrapGApps = pkgs.stdenv.isLinux;

          qtWrapperArgs =
            pkgs.lib.optionals pkgs.stdenv.isDarwin [
              "--set"
              "QT_QPA_PLATFORM"
              "cocoa"
            ]
            ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
              # Do not inherit potentially incompatible user-profile GIO modules.
              "--set"
              "GIO_EXTRA_MODULES"
              "${pkgs.glib-networking}/lib/gio/modules"
            ];

          preFixup = pkgs.lib.optionalString pkgs.stdenv.isLinux ''
            qtWrapperArgs+=("''${gappsWrapperArgs[@]}")
          '';

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

            # Automated testing
            gtest

            # ONNX Runtime for neural-network chroma decoding (nn_ntsc_chroma_sink_stage).
            # Sourced from nixpkgs-unstable because nixos-25.11 ships 1.22.2 which is
            # insufficient; chroma_net_v2.onnx requires runtime ≥ 1.23.2.
            # Both outputs are required: 'out' for the shared library, 'dev' for
            # the CMake config files and C++ API headers.
            pkgs-unstable.onnxruntime
            pkgs-unstable.onnxruntime.dev
          ] ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
            # Provide GTK schemas/settings used by Qt's native integration paths.
            gtk3
            gsettings-desktop-schemas
            glib-networking
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
            # Tell CMake where to find the ezpwd Reed-Solomon headers
            "-DEZPWD_INCLUDE_DIR=${ezpwd-headers}"
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

        # Build MkDocs documentation as a separate flake package
        decode-orc-docs = pkgs.stdenv.mkDerivation {
          pname = "decode-orc-docs";
          version = version;

          src = self;

          nativeBuildInputs = [
            mkdocsPythonEnv
          ];

          buildPhase = ''
            mkdocs build
          '';

          installPhase = ''
            mkdir -p $out
            cp -r site/* $out/
          '';

          meta = with pkgs.lib; {
            description = "Decode-Orc documentation site";
            homepage = "https://github.com/simoninns/decode-orc";
            license = licenses.gpl3Plus;
            platforms = platforms.all;
            maintainers = [ ];
          };
        };

      in
      {
        # Packages that can be built with `nix build`
        packages = {
          default = decode-orc;
          decode-orc = decode-orc;
          docs = decode-orc-docs;
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
        devShells.default = pkgs.mkShell.override { inherit stdenv; } {
          inputsFrom = [ decode-orc ];

          packages = with pkgs; [
            # Additional development tools
            cmake-format
            clang-tools
            ccache
            doxygen
            graphviz
            mkdocsPythonEnv
            ninja
            zip

            # Version control
            git
          ] ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
            gdb
            valgrind
            perf
            hotspot
            heaptrack
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
            
            # Expose ezpwd headers for manual cmake runs inside nix develop
            export EZPWD_INCLUDE_DIR=${ezpwd-headers}

            # Build CMAKE_PREFIX_PATH from all build inputs so that IDEs (e.g. CLion)
            # launched from this shell can run cmake without extra configuration.
            export CMAKE_PREFIX_PATH="$(echo $buildInputs $nativeBuildInputs | tr ' ' '\n' | tr '\n' ':')"

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
