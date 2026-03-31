# Compiling from source using Nix (Linux and Mac)

The project requires [Nix](https://nixos.org/) for deterministic, reproducible builds.  You must install nix in order to build from source.

## Entering the nix development environment

Use the following command to enter a nix development environment which will provided the required dependencies:
```
nix develop
```

## Build the project using nix

Use the following command to build the project (builds orc-gui, orc-cli and integrates encode-orc as a component):

```
nix build
```

Note: encode-orc is built and integrated into orc-gui and orc-cli, not as a separate executable.

## Running the GUI application

Use the following command to run the orc-gui application:

```
nix run .#orc-gui
```

## Installing the application

To install the application persistently to your user profile:

```
nix profile install .
```

This will:
- Install both `orc-gui` and `orc-cli` to your PATH
- Install the desktop file to `~/.nix-profile/share/applications/`
- Make the application appear in your desktop environment's application menu

After installation, you can run the applications directly:

```
orc-gui
orc-cli
```

To uninstall:

```
nix profile remove decode-orc
```

### NixOS system-wide installation

On NixOS, you can install system-wide by adding to your `configuration.nix`:

```nix
environment.systemPackages = [
  (pkgs.callPackage /path/to/decode-orc {})
];
```