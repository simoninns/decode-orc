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

## Troubleshooting: "file not found" when opening TBC files

Features like Quick Project open sibling files next to the file you select
(for example `.tbc.db` metadata or a matching `.tbcy`/`.tbcc` pair), so the
whole directory must be visible inside the sandbox. The Flatpak grants access
to your home directory and to drives mounted under `/run/media`, `/media`,
and `/mnt`. If your captures live somewhere else, grant access manually:

```bash
flatpak override --user --filesystem=/path/to/your/captures io.github.simoninns.decode-orc
```

### Network drives (SMB/NFS)

- **Kernel mounts** (fstab, `mount -t cifs`, systemd automount): work as long
  as the mount point is under your home directory, `/run/media`, `/media`, or
  `/mnt`. For other locations use the `flatpak override` command above.
- **GNOME (Nautilus) network browsing** (`smb://...`): works — the sandbox has
  access to GVFS mounts.
- **KDE (Dolphin) network browsing**: not supported — KDE's kio-fuse uses a
  randomized runtime path that a Flatpak cannot be granted access to. Mount
  the share with a kernel cifs mount instead.

## Uninstall

```bash
flatpak uninstall --user io.github.simoninns.decode-orc
```
