# Issue Reporting

## Reporting Crashes

If Decode Orc crashes unexpectedly, it will automatically create a **diagnostic bundle** to help identify and fix the problem.

### What Happens When a Crash Occurs

When the application crashes, you'll see a message showing where the diagnostic bundle was saved:

**For orc-gui:**
- A dialog box will appear with the crash information and bundle location
- Example: `/home/username/Documents/decode-orc-crashes/crash_bundle_20260111_143022.zip`

**For orc-cli:**
- A message is printed to the terminal showing the bundle location
- Example: `./crash_bundle_20260111_143022.zip` (in your current directory)

### Crash Bundle Contents

The crash bundle is a ZIP file containing:

- **crash_info.txt** - Detailed crash report including:
  - Application version (git commit hash)
  - System information (OS, CPU, memory)
  - Stack backtrace showing where the crash occurred
  - Timestamp of the crash
  
- **README.txt** - Instructions for reporting the issue

- **Log files** - Any application log files (if available)

- **Coredump** - Memory dump file (if available, may be large)

### How to Report a Crash

1. Go to the [GitHub Issues page](https://github.com/simoninns/decode-orc/issues){target="_blank"}

2. Click **"New Issue"** and select the bug report template

3. **Attach the crash bundle ZIP file**
   - If the file is smaller than 25MB, attach it directly to the issue
   - If it's larger, upload it to a file sharing service (Google Drive, Dropbox, etc.) and include the link

4. **Copy the contents of crash_info.txt** into your issue description
   - This helps us quickly see the crash details
   - You can redact any sensitive file paths if needed

5. **Describe what you were doing** when the crash occurred:
   - What project were you working on?
   - What operation did you just start/complete?
   - Can you reproduce the crash?
   - Any recent changes to your system or workflow?

### Privacy Note

The crash bundle may contain:
- File paths from your system
- Project file names
- Log file contents
- Memory contents (in the coredump)

**Please review the contents** of `crash_info.txt` before uploading to ensure you're comfortable sharing this information publicly.

### Crash Bundle Locations

**orc-gui:**
- Primary: `~/Documents/decode-orc-crashes/`
- Fallback: `~/.decode-orc-crashes/` or current directory

**orc-cli:**
- Saved in your current working directory

### If You Can't Find the Crash Bundle

If the application crashed but you can't find the bundle:

1. Check the terminal output or error dialog for the exact path
2. Search your home directory for files named `crash_bundle_*.zip`
3. Look in the current working directory where you ran the command

If no bundle was created, please report the crash manually with:
- Application version (run with `--version` flag)
- Operating system and version
- What you were doing when it crashed
- Any error messages shown

## Reporting Bugs (Non-Crash Issues)

For bugs that don't cause crashes, please provide:

1. **Steps to reproduce** the issue
2. **Expected behavior** vs actual behavior
3. **Application version** and platform
4. **Screenshots or videos** if helpful
5. **Log files** if available (use `--log-file` option)

## Feature Requests

For new features or enhancements, please describe:

1. **What you want to achieve** and why
2. **Current workarounds** you're using (if any)
3. **Use cases** where this would be helpful

## Getting Help

For questions or help using Decode Orc (not bugs):

- Check the [user documentation](https://simoninns.github.io/decode-orc-docs/)
- Review existing [GitHub Issues](https://github.com/simoninns/decode-orc/issues){target="_blank"} (someone may have had the same question)
- [The Domesday86 Discord Server](https://discord.com/invite/pVVrrxd){target="_blank"}