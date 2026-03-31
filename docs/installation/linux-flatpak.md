# Linux Flatpak Installation

## Download the latest release

1. Open the GitHub releases page: [Decode-Orc Releases](https://github.com/simoninns/decode-orc/releases){target="_blank"}
2. In the latest release, download the Flatpak bundle named like `Decode-Orc-<version>-linux.flatpak`.

## Install the Flatpak bundle

1. Install Flatpak (if you do not already have it): https://flatpak.org/setup/
2. Install the bundle to your user account:

```bash
flatpak install --user -y /path/to/Decode-Orc-<version>-linux.flatpak
```

Note: If you receive an error about a missing KDE runtime you may need to use the following command before installing the Decode-Orc flatpak (ensure the version number matches with the runtime requested by Decode-Orc):

```bash
flatpak install flathub org.kde.Platform//6.9
```

## Run the app

Launch the GUI:

```bash
flatpak run io.github.simoninns.decode-orc
```

## Use `orc-cli` from the command line

`orc-cli` is available inside the Flatpak sandbox. Run it like this:

```bash
flatpak run --command=orc-cli io.github.simoninns.decode-orc --help
```

If you want a convenience wrapper, add a shell alias:

```bash
alias orc-cli='flatpak run --command=orc-cli io.github.simoninns.decode-orc'
```

## Uninstall

```bash
flatpak uninstall --user io.github.simoninns.decode-orc
```
