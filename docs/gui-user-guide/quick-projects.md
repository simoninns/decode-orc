# Quick Projects

The Quick Project feature provides the fastest way to get started with decoding a TBC (Time Base Corrected) file. Instead of manually creating a new project and configuring all the settings, a quick project automatically sets up everything you need with just a few clicks or a single command.

![](assets/quick-project.png)

### Using Quick Project in the GUI

#### Menu Option

- Open **File** → **Quick Project...**
- A file dialog will open asking you to select a video file
- Choose a `.tbc`, `.tbcc`, or `.tbcy` file (TBC captures), or a `.composite`, `.y`, or `.c` file (CVBS captures) from your system
- The application will automatically:
    - Detect the video format based on the file metadata (`.tbc.db` for TBC, `.meta` for CVBS)
    - Create a new project
    - Configure the appropriate video system (PAL/NTSC/PAL-M) and source type
    - Build a pipeline appropriate to the decoder that produced the source (see [Pipeline layout](#pipeline-layout) below)
    - Select the source stage (with **View → Show Preview on Selection** enabled, the preview opens automatically)

### Pipeline layout

The stages a quick project creates depend on the decoder recorded in the source metadata:

- **ld-decode sources** get a source stage, a **Dropout Correction** stage, and a **Video Sink** stage, connected in sequence (`source → dropout correction → video sink`). If the source has an EFM sidecar, an **EFM Decoder Sink** is also added and connected to the output of the Dropout Correction stage, so both the video sink and the EFM audio decode are fed from the corrected video.
- **All other sources** (for example vhs-decode) get just a source stage and a **Video Sink** stage, connected together.

The decoder is read from the source metadata: the `decoder` field of the `.tbc.db` (TBC) or `.meta` (CVBS) database. Older TBC captures with only legacy JSON (`.tbc.json`) metadata do not carry a reliable decoder identity and are always treated as non-ld-decode, so they receive the plain source → video sink pipeline.

### Loading a TBC from the Command Line

#### Using the `--quick` Option

To create a quick project from the command line, use the `--quick` flag followed by the file path:

```bash
orc-gui --quick /path/to/your/file.tbc
```

You can also pass a TBC file (`.tbc`, `.tbcc`, `.tbcy`) directly as an argument, and the application will automatically detect it's a video file and create a quick project (CVBS files require the `--quick` flag):

```bash
orc-gui /path/to/your/file.tbc
```

The application supports the following video file formats:

- `.tbc` - Composite TBC video files
- `.tbcy` / `.tbcc` - Y/C TBC video files (luma and chroma; both files required as a pair)
- `.composite` - Composite CVBS video files
- `.y` / `.c` - Y/C CVBS video files (both files required as a pair)

### Tips

- Quick projects are ideal for quickly previewing and analyzing video files without project setup overhead
- If you need to save your project for later, use **File** → **Save Project As** and give it a meaningful name
- All standard editing and analysis tools remain available after creating a quick project
