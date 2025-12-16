# Inter-tool metadata format

In order for all tools in the ld-decode tool-chain to communicate metadata about video and non-video capture content, the tools use an SQLite database to store and process information.

Please note that this metadata format is *internal* to the ld-decode project and should not be used by external tools (as the metadata format is subject to change without notice).  Instead, export metadata for external tools through applications such as [ld-export-metadata](../Tools/ld-export-metadata.md) to ensure that APIs are maintained through proper CI/CD testing within the ld-decode repository.

## SQLite Schema

```sql
------------------------------------------------------------------
-- Schema Versioning
------------------------------------------------------------------
PRAGMA user_version = 1;

------------------------------------------------------------------
-- 1. Capture-level metadata (one per JSON file)
------------------------------------------------------------------
CREATE TABLE capture (
    capture_id INTEGER PRIMARY KEY,
    system TEXT NOT NULL
        CHECK (system IN ('NTSC','PAL','PAL_M')),
    decoder TEXT NOT NULL
        CHECK (decoder IN ('ld-decode','vhs-decode')),
    git_branch TEXT,
    git_commit TEXT,

    video_sample_rate REAL,
    active_video_start INTEGER,
    active_video_end INTEGER,
    field_width INTEGER,
    field_height INTEGER,
    number_of_sequential_fields INTEGER,

    colour_burst_start INTEGER,
    colour_burst_end INTEGER,
    is_mapped INTEGER
        CHECK (is_mapped IN (0,1)),
    is_subcarrier_locked INTEGER
        CHECK (is_subcarrier_locked IN (0,1)),
    is_widescreen INTEGER
        CHECK (is_widescreen IN (0,1)),
    white_16b_ire INTEGER,
    black_16b_ire INTEGER,

    capture_notes TEXT -- was JSON tape_format
);

------------------------------------------------------------------
-- 2. PCM Audio Parameters (one per capture)
------------------------------------------------------------------
CREATE TABLE pcm_audio_parameters (
    capture_id INTEGER PRIMARY KEY
        REFERENCES capture(capture_id) ON DELETE CASCADE,
    bits INTEGER,
    is_signed INTEGER
        CHECK (is_signed IN (0,1)),
    is_little_endian INTEGER
        CHECK (is_little_endian IN (0,1)),
    sample_rate REAL
);

------------------------------------------------------------------
-- 3. Field metadata
------------------------------------------------------------------
CREATE TABLE field_record (
    capture_id INTEGER NOT NULL
        REFERENCES capture(capture_id) ON DELETE CASCADE,
    -- Note: Original JSON seqNo was indexed from 1, the field_id
    -- will be the original seqNo - 1 to zero-index the ID
    field_id INTEGER NOT NULL,
    audio_samples INTEGER,
    decode_faults INTEGER,
    disk_loc REAL,
    efm_t_values INTEGER,
    field_phase_id INTEGER,
    file_loc INTEGER,
    is_first_field INTEGER
        CHECK (is_first_field IN (0,1)),
    median_burst_ire REAL,
    pad INTEGER
        CHECK (pad IN (0,1)),
    sync_conf INTEGER,

    -- NTSC specific fields (NULL for other formats)
    ntsc_is_fm_code_data_valid INTEGER
        CHECK (ntsc_is_fm_code_data_valid IN (0,1)),
    ntsc_fm_code_data INTEGER,
    ntsc_field_flag INTEGER
        CHECK (ntsc_field_flag IN (0,1)),
    ntsc_is_video_id_data_valid INTEGER
        CHECK (ntsc_is_video_id_data_valid IN (0,1)),
    ntsc_video_id_data INTEGER,
    ntsc_white_flag INTEGER
        CHECK (ntsc_white_flag IN (0,1)),

    PRIMARY KEY (capture_id, field_id)
);

------------------------------------------------------------------
-- 4. VITS metrics (optional) - one per field
------------------------------------------------------------------
CREATE TABLE vits_metrics (
    capture_id INTEGER NOT NULL,
    field_id INTEGER NOT NULL,
    b_psnr REAL,
    w_snr REAL,
    FOREIGN KEY (capture_id, field_id)
        REFERENCES field_record(capture_id, field_id)
        ON DELETE CASCADE,
    PRIMARY KEY (capture_id, field_id)
);

------------------------------------------------------------------
-- 5. VBI data (optional) - stores 3 VBI data values per field
------------------------------------------------------------------
CREATE TABLE vbi (
    capture_id INTEGER NOT NULL,
    field_id INTEGER NOT NULL,
    vbi0 INTEGER NOT NULL, -- VBI line 16 data
    vbi1 INTEGER NOT NULL, -- VBI line 17 data  
    vbi2 INTEGER NOT NULL, -- VBI line 18 data
    FOREIGN KEY (capture_id, field_id)
        REFERENCES field_record(capture_id, field_id)
        ON DELETE CASCADE,
    PRIMARY KEY (capture_id, field_id)
);

------------------------------------------------------------------
-- 6. Drop-out elements (optional)
------------------------------------------------------------------
CREATE TABLE drop_outs (
    capture_id INTEGER NOT NULL,
    field_id INTEGER NOT NULL, 
    field_line INTEGER NOT NULL,
    startx INTEGER NOT NULL,
    endx INTEGER NOT NULL,
    PRIMARY KEY (capture_id, field_id, field_line, startx, endx),
    FOREIGN KEY (capture_id, field_id)
        REFERENCES field_record(capture_id, field_id)
        ON DELETE CASCADE
);

------------------------------------------------------------------
-- 7. VITC data (optional) - stores 8 VITC data values per field
------------------------------------------------------------------
CREATE TABLE vitc (
    capture_id INTEGER NOT NULL,
    field_id INTEGER NOT NULL,
    vitc0 INTEGER NOT NULL, -- VITC data element 0
    vitc1 INTEGER NOT NULL, -- VITC data element 1
    vitc2 INTEGER NOT NULL, -- VITC data element 2
    vitc3 INTEGER NOT NULL, -- VITC data element 3
    vitc4 INTEGER NOT NULL, -- VITC data element 4
    vitc5 INTEGER NOT NULL, -- VITC data element 5
    vitc6 INTEGER NOT NULL, -- VITC data element 6
    vitc7 INTEGER NOT NULL, -- VITC data element 7
    FOREIGN KEY (capture_id, field_id)
        REFERENCES field_record(capture_id, field_id)
        ON DELETE CASCADE,
    PRIMARY KEY (capture_id, field_id)
);

------------------------------------------------------------------
-- 8. Closed Caption data (optional) - one per field
------------------------------------------------------------------
CREATE TABLE closed_caption (
    capture_id INTEGER NOT NULL,
    field_id INTEGER NOT NULL,
    data0 INTEGER, -- First closed caption byte (-1 if invalid)
    data1 INTEGER, -- Second closed caption byte (-1 if invalid)
    FOREIGN KEY (capture_id, field_id)
        REFERENCES field_record(capture_id, field_id)
        ON DELETE CASCADE,
    PRIMARY KEY (capture_id, field_id)
);
```

See the TBC library for details

# ld-decode TBC file format
The output from ld-decode.py is a stream of 16-bit unsigned values; each value representing a single grey-scale value.  The file extension used by ld-decode is .tbc for both NTSC and PAL decoded frames aptly named the Time Base Corrected format.

PAL output is 1135x625 16-bit values. (280mbps) (2.1GB/min) (126GB/hour)

NTSC output is 910x525 16-bit values. (226.5mbps) (1.7GB/min) (102GB/hour)

The 16-bit grey-scale values used by the output format are scaled representations of the standard 8-bit digital component values (i.e. an 8-bit right shift of the value will provide the standard 8-bit digital component intensity values).

The frequency values for .tbc to analogue CVBS playback are the following:

PAL - 17727262 Hz

NTSC - 14318181 Hz

See the TBC library for details