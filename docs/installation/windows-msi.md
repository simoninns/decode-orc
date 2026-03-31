# Windows MSI Installation

## Download the latest release

1. Open the GitHub releases page: [Decode-Orc Releases](https://github.com/simoninns/decode-orc/releases)
2. In the latest release, download the MSI named like `Decode-Orc-<version>-windows-x64.msi`.

## Install from the MSI

1. Double-click the downloaded MSI file.
2. Follow the Windows Installer wizard:
   - Review the license agreement and click "I Agree" to continue.
   - Choose the installation location (default: `Program Files\Decode-Orc`).
   - Select whether to create Start Menu and Desktop shortcuts.
   - Click "Install" to begin installation.
3. Wait for the installation to complete and click "Finish".

## Unsigned MSI - Additional Steps Required

The MSI installer is **unsigned**, which means Windows Defender SmartScreen may block installation. Follow these steps:

### Option A: Install via Windows Defender SmartScreen Prompt (Recommended)

1. When you try to run the MSI, you may see a "Windows Defender SmartScreen prevented an unrecognized app from starting" dialog.
2. Click **"More info"** to expand the dialog.
3. Click **"Run anyway"** to proceed with installation.
4. The installer will then launch normally.

### Option B: Disable SmartScreen (Not Recommended)

If you don't see the prompt, you may need to manually disable SmartScreen protection temporarily:

1. Open **Windows Security**.
2. Go to **App & browser control**.
3. Under **SmartScreen for Microsoft Edge**, toggle to **Off**.
4. Run the MSI installer.
5. Re-enable SmartScreen protection after installation.

### Option C: Override via Command Line (Advanced)

If the GUI method doesn't work, you can install via PowerShell or Command Prompt:

```powershell
# In PowerShell as Administrator:
msiexec /i "C:\path\to\Decode-Orc-<version>-windows-x64.msi" /quiet
```

Replace `C:\path\to\Decode-Orc-<version>-windows-x64.msi` with the actual path to the downloaded MSI file.

## Launching the Application

After installation, you can launch Decode-Orc:

- **Via Start Menu**: Search for "Decode-Orc" and click the application.
- **Via Desktop Shortcut**: Double-click the "Decode-Orc" shortcut on your desktop (if created during installation).
- **Via File Explorer**: Navigate to `Program Files\Decode-Orc\bin\` and double-click `orc-gui.exe`.

## Using `orc-cli` from the command line

The command-line tool `orc-cli` is included in the installation. You can run it from the Command Prompt or PowerShell:

```cmd
"C:\Program Files\Decode-Orc\bin\orc-cli.exe" --help
```

To use `orc-cli` from any directory, add the installation directory to your system PATH:

1. Press `Win + X` and select **System**.
2. Click **Advanced system settings**.
3. Click **Environment Variables**.
4. Under **System variables**, select **Path** and click **Edit**.
5. Click **New** and add: `C:\Program Files\Decode-Orc\bin`
6. Click **OK** on all dialogs.
7. Restart your terminal.

You can then run:

```cmd
orc-cli --help
```

## Uninstalling

To remove Decode-Orc:

1. Open **Control Panel** > **Programs** > **Programs and Features**.
2. Find **Decode-Orc** in the list.
3. Click **Uninstall** and confirm.
4. Follow the uninstall wizard.

Alternatively, you can uninstall via the MSI:

```cmd
msiexec /x "Decode-Orc-<version>-windows-x64.msi"
```
