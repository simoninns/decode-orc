/*
 * File:        cvbs_sink_stage_deps.cpp
 * Module:      orc-core
 * Purpose:     CVBSSinkStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "cvbs_sink_stage_deps.h"

#include <orc/stage/cvbs_signal_constants.h>
#include <orc/stage/frame_descriptor.h>
#include <orc/stage/logging.h>
#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <utility>
#include <vector>

namespace orc {

namespace {

// ---------------------------------------------------------------------------
// Video-system helpers
// ---------------------------------------------------------------------------

// Video Standard Preset name for the .meta preset column. Uses the spec
// spelling (PAL_M with an underscore), unlike video_system_to_string().
const char* preset_name(VideoSystem system) {
  switch (system) {
    case VideoSystem::PAL:
      return "PAL";
    case VideoSystem::NTSC:
      return "NTSC";
    case VideoSystem::PAL_M:
      return "PAL_M";
    default:
      return nullptr;
  }
}

// Spec blanking level for CVBS_S16_FSC encoding when the representation
// provides no SourceParameters.
int32_t default_blanking_level(VideoSystem system) {
  // EBU Tech. 3280-E §1.1 (PAL) / SMPTE 244M-2003 (NTSC, PAL_M).
  return (system == VideoSystem::PAL) ? kPalBlanking : kNtscBlanking;
}

// ---------------------------------------------------------------------------
// Per-frame sidecar bookkeeping
// ---------------------------------------------------------------------------

// One dropout_run row (dropout extension format).
struct DropoutRow {
  FrameID frame_id = 0;  // output-file frame index
  uint64_t sample_start = 0;
  uint32_t sample_count = 0;
  uint8_t severity = 0;
};

// One efm_frame / ac3_frame row (EFM / AC3 extension formats).
struct ExtensionRow {
  FrameID frame_id = 0;  // output-file frame index
  uint64_t offset = 0;
  uint32_t count = 0;
};

// ---------------------------------------------------------------------------
// SQLite helpers
// ---------------------------------------------------------------------------

// Create a fresh SQLite database at path (removing any existing file) and
// run the given schema SQL. Returns nullptr and sets error_message on
// failure; the caller owns the returned handle.
sqlite3* create_sidecar_db(const std::string& path, const char* schema_sql,
                           std::string& error_message) {
  std::error_code ec;
  std::filesystem::remove(path, ec);

  sqlite3* db = nullptr;
  if (sqlite3_open_v2(path.c_str(), &db,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                      nullptr) != SQLITE_OK) {
    error_message = "Failed to create '" + path +
                    "': " + (db ? sqlite3_errmsg(db) : "unknown error");
    if (db) sqlite3_close(db);
    return nullptr;
  }

  char* exec_err = nullptr;
  if (sqlite3_exec(db, schema_sql, nullptr, nullptr, &exec_err) != SQLITE_OK) {
    error_message = "Failed to initialise '" + path +
                    "': " + (exec_err ? exec_err : "unknown error");
    sqlite3_free(exec_err);
    sqlite3_close(db);
    return nullptr;
  }
  return db;
}

// Write the core <base>.meta metadata database.
// CVBS file format spec: Metadata Schema (PRAGMA user_version = 8).
bool write_core_metadata(const std::string& meta_path, VideoSystem system,
                         const CVBSSinkWriteConfig& config,
                         uint64_t frames_written,
                         std::optional<int32_t> black_level,
                         bool has_nonstandard_values,
                         std::optional<bool> audio_locked,
                         std::string& error_message) {
  constexpr const char* kSchema =
      "PRAGMA user_version = 8;"
      "CREATE TABLE cvbs_file ("
      "    cvbs_file_id                INTEGER PRIMARY KEY,"
      "    preset                      TEXT    NOT NULL"
      "        CHECK (preset IN ('NTSC', 'PAL', 'PAL_M')),"
      "    sample_encoding_preset      TEXT    NOT NULL"
      "        CHECK (sample_encoding_preset IN ('CVBS_U10_4FSC', "
      "'CVBS_U16_4FSC', 'RAW_S16_28M', 'RAW_S16_40M', 'CVBS_TPG21_4FSC', "
      "'CVBS_S16_FSC')),"
      "    signal_state_preset         TEXT    NOT NULL"
      "        CHECK (signal_state_preset IN ("
      "            'STANDARD_TBC_LOCKED',"
      "            'STANDARD_TBC_UNLOCKED',"
      "            'STANDARD_RAW',"
      "            'NONSTANDARD_TBC_LOCKED',"
      "            'NONSTANDARD_TBC_UNLOCKED',"
      "            'NONSTANDARD_RAW'"
      "        )),"
      "    signal_type                 TEXT    NOT NULL"
      "        CHECK (signal_type IN ('composite', 'yc')),"
      "    decoder                     TEXT    NOT NULL,"
      "    git_branch                  TEXT,"
      "    git_commit                  TEXT,"
      "    number_of_sequential_frames INTEGER"
      "        CHECK (number_of_sequential_frames IS NULL OR "
      "number_of_sequential_frames >= 1),"
      "    black_level                 INTEGER,"
      "    has_nonstandard_values      BOOLEAN,"
      "    audio_locked                BOOLEAN,"
      "    capture_notes               TEXT"
      ");";

  sqlite3* db = create_sidecar_db(meta_path, kSchema, error_message);
  if (!db) return false;

  constexpr const char* kInsert =
      "INSERT INTO cvbs_file (cvbs_file_id, preset, sample_encoding_preset, "
      "signal_state_preset, signal_type, decoder, number_of_sequential_frames, "
      "black_level, has_nonstandard_values, audio_locked, capture_notes) "
      "VALUES (1, ?, ?, 'STANDARD_TBC_LOCKED', ?, 'other', ?, ?, ?, ?, ?)";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, kInsert, -1, &stmt, nullptr) != SQLITE_OK) {
    error_message =
        "Failed to prepare metadata insert: " + std::string(sqlite3_errmsg(db));
    sqlite3_close(db);
    return false;
  }

  sqlite3_bind_text(stmt, 1, preset_name(system), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, cvbs_sample_encoding_name(config.sample_encoding),
                    -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, config.signal_type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(frames_written));
  if (black_level.has_value()) {
    sqlite3_bind_int(stmt, 5, *black_level);
  } else {
    sqlite3_bind_null(stmt, 5);
  }
  if (has_nonstandard_values) {
    sqlite3_bind_int(stmt, 6, 1);
  } else {
    sqlite3_bind_null(stmt, 6);
  }
  if (audio_locked.has_value()) {
    sqlite3_bind_int(stmt, 7, *audio_locked ? 1 : 0);
  } else {
    sqlite3_bind_null(stmt, 7);
  }
  if (!config.capture_notes.empty()) {
    sqlite3_bind_text(stmt, 8, config.capture_notes.c_str(), -1,
                      SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(stmt, 8);
  }

  const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
  if (!ok) {
    error_message =
        "Failed to write metadata row: " + std::string(sqlite3_errmsg(db));
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return ok;
}

// Write the <base>.dropouts.meta sidecar.
// CVBS dropout extension format (PRAGMA user_version = 5).
bool write_dropout_sidecar(const std::string& path,
                           const std::vector<DropoutRow>& rows,
                           std::string& error_message) {
  constexpr const char* kSchema =
      "PRAGMA user_version = 5;"
      "CREATE TABLE dropout_run ("
      "    cvbs_file_id    INTEGER NOT NULL,"
      "    frame_id        INTEGER NOT NULL"
      "        CHECK (frame_id >= 0),"
      "    sample_start    INTEGER NOT NULL"
      "        CHECK (sample_start >= 0),"
      "    sample_count    INTEGER NOT NULL"
      "        CHECK (sample_count > 0),"
      "    severity        INTEGER NOT NULL"
      "        CHECK (severity >= 0 AND severity <= 100),"
      "    PRIMARY KEY (cvbs_file_id, frame_id, sample_start)"
      ");"
      "CREATE INDEX idx_dropout_run_frame"
      "    ON dropout_run (cvbs_file_id, frame_id);";

  sqlite3* db = create_sidecar_db(path, kSchema, error_message);
  if (!db) return false;

  constexpr const char* kInsert =
      "INSERT OR REPLACE INTO dropout_run (cvbs_file_id, frame_id, "
      "sample_start, sample_count, severity) VALUES (1, ?, ?, ?, ?)";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, kInsert, -1, &stmt, nullptr) != SQLITE_OK) {
    error_message =
        "Failed to prepare dropout insert: " + std::string(sqlite3_errmsg(db));
    sqlite3_close(db);
    return false;
  }

  bool ok = true;
  sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
  for (const DropoutRow& row : rows) {
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(row.frame_id));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(row.sample_start));
    sqlite3_bind_int(stmt, 3, static_cast<int>(row.sample_count));
    sqlite3_bind_int(stmt, 4,
                     static_cast<int>(std::min<uint8_t>(row.severity, 100)));
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      error_message =
          "Failed to write dropout row: " + std::string(sqlite3_errmsg(db));
      ok = false;
      break;
    }
    sqlite3_reset(stmt);
  }
  sqlite3_exec(db, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return ok;
}

// Write an EFM or AC3 frame-index sidecar (<base>.efm.meta / <base>.ac3.meta).
// EFM extension format / AC3 extension format (PRAGMA user_version = 1);
// the two schemas are identical apart from the table and index names.
bool write_extension_sidecar(const std::string& path,
                             const std::string& table_name,
                             const std::vector<ExtensionRow>& rows,
                             std::string& error_message) {
  const std::string schema =
      "PRAGMA user_version = 1;"
      "CREATE TABLE " +
      table_name +
      " ("
      "    cvbs_file_id    INTEGER NOT NULL,"
      "    frame_id        INTEGER NOT NULL"
      "        CHECK (frame_id >= 0),"
      "    t_value_offset  INTEGER NOT NULL"
      "        CHECK (t_value_offset >= 0),"
      "    t_value_count   INTEGER NOT NULL"
      "        CHECK (t_value_count >= 0),"
      "    PRIMARY KEY (cvbs_file_id, frame_id)"
      ");"
      "CREATE INDEX idx_" +
      table_name + "_frame ON " + table_name + " (cvbs_file_id, frame_id);";

  sqlite3* db = create_sidecar_db(path, schema.c_str(), error_message);
  if (!db) return false;

  const std::string insert =
      "INSERT INTO " + table_name +
      " (cvbs_file_id, frame_id, t_value_offset, t_value_count) "
      "VALUES (1, ?, ?, ?)";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, insert.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    error_message = "Failed to prepare " + table_name +
                    " insert: " + std::string(sqlite3_errmsg(db));
    sqlite3_close(db);
    return false;
  }

  bool ok = true;
  sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
  for (const ExtensionRow& row : rows) {
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(row.frame_id));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(row.offset));
    sqlite3_bind_int(stmt, 3, static_cast<int>(row.count));
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      error_message = "Failed to write " + table_name +
                      " row: " + std::string(sqlite3_errmsg(db));
      ok = false;
      break;
    }
    sqlite3_reset(stmt);
  }
  sqlite3_exec(db, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return ok;
}

// ---------------------------------------------------------------------------
// WAV helpers
// ---------------------------------------------------------------------------

void append_le16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void append_le32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

// 44-byte canonical RIFF/WAVE header for 16-bit stereo PCM.
// CVBS file format spec, Audio Data: frame-locked audio carries the integer
// approximation of the preset rate in nSamplesPerSec (44100 for PAL, 44056
// for NTSC/PAL_M); the authoritative rate is the preset-defined rational.
std::vector<uint8_t> make_wav_header(VideoSystem system, uint32_t data_bytes) {
  const uint32_t sample_rate = (system == VideoSystem::PAL) ? 44100u : 44056u;
  constexpr uint16_t kChannels = 2;
  constexpr uint16_t kBitsPerSample = 16;
  constexpr uint16_t kBlockAlign = kChannels * kBitsPerSample / 8;

  std::vector<uint8_t> header;
  header.reserve(44);
  header.insert(header.end(), {'R', 'I', 'F', 'F'});
  append_le32(header, 36 + data_bytes);
  header.insert(header.end(), {'W', 'A', 'V', 'E'});
  header.insert(header.end(), {'f', 'm', 't', ' '});
  append_le32(header, 16);  // fmt chunk size
  append_le16(header, 1);   // PCM
  append_le16(header, kChannels);
  append_le32(header, sample_rate);
  append_le32(header, sample_rate * kBlockAlign);  // byte rate
  append_le16(header, kBlockAlign);
  append_le16(header, kBitsPerSample);
  header.insert(header.end(), {'d', 'a', 't', 'a'});
  append_le32(header, data_bytes);
  return header;
}

// Remove any output files from a previous run so a re-run never leaves a
// stale sidecar describing data that is no longer being written.
void remove_stale_outputs(const std::string& base) {
  static const char* kExtensions[] = {
      ".composite", ".y",        ".c",   ".meta",    ".dropouts.meta",
      ".efm",       ".efm.meta", ".ac3", ".ac3.meta"};
  std::error_code ec;
  for (const char* ext : kExtensions) {
    std::filesystem::remove(base + ext, ec);
  }
  std::filesystem::remove(base + "_audio_00.wav", ec);
}

}  // namespace

void CVBSSinkStageDeps::init(TriggerProgressCallback progress_callback,
                             std::atomic<bool>* cancel_requested) {
  progress_callback_ = std::move(progress_callback);
  cancel_requested_ = cancel_requested;
}

CVBSSinkWriteResult CVBSSinkStageDeps::write_cvbs(
    const VideoFrameRepresentation* representation,
    const CVBSSinkWriteConfig& config) {
  if (!representation) {
    return {false, 0, "Input representation is null"};
  }

  const auto frame_rng = representation->frame_range();
  const uint64_t total_frames = frame_rng.count();
  if (total_frames == 0) {
    return {false, 0, "Input representation contains no frames"};
  }

  // --- Determine the video system and blanking level ---
  VideoSystem system = VideoSystem::Unknown;
  int32_t blanking_level = -1;
  std::optional<int32_t> black_level_override;
  bool has_nonstandard_values = false;

  if (const auto params = representation->get_video_parameters()) {
    system = params->system;
    blanking_level = params->blanking_level;
    has_nonstandard_values = params->has_nonstandard_values;
    if (has_nonstandard_values) black_level_override = params->black_level;
  } else {
    for (FrameID fid = frame_rng.first; fid <= frame_rng.last; ++fid) {
      const auto desc = representation->get_frame_descriptor(fid);
      if (!desc.has_value()) continue;
      system = desc->system;
      if (desc->black_level_override.has_value()) {
        black_level_override = desc->black_level_override;
        has_nonstandard_values = true;
      }
      break;
    }
  }

  if (!preset_name(system)) {
    return {false, 0, "Cannot determine the video system of the input"};
  }
  if (blanking_level < 0) blanking_level = default_blanking_level(system);

  // --- Signal type ---
  const bool yc = (config.signal_type == "yc");
  if (!yc && config.signal_type != "composite") {
    return {false, 0, "Unknown signal_type '" + config.signal_type + "'"};
  }
  if (yc && !representation->has_separate_channels()) {
    return {false, 0,
            "signal_type 'yc' requires an input with separate Y/C channels"};
  }

  const std::string base = derive_cvbs_output_base(config.output_base_path);
  if (base.empty()) {
    return {false, 0, "Output path is empty"};
  }

  remove_stale_outputs(base);

  // --- Open payload stream(s) ---
  const std::string primary_path = base + (yc ? ".y" : ".composite");
  std::ofstream primary(primary_path, std::ios::binary | std::ios::trunc);
  if (!primary) {
    return {false, 0, "Failed to open output file: " + primary_path};
  }

  std::ofstream chroma;
  const std::string chroma_path = base + ".c";
  if (yc) {
    chroma.open(chroma_path, std::ios::binary | std::ios::trunc);
    if (!chroma) {
      return {false, 0, "Failed to open output file: " + chroma_path};
    }
  }

  // --- Extension streams (opened only when the input carries the data) ---
  // This sink currently writes audio track 0 only, and only when locked.
  const auto audio_track_desc = representation->get_audio_track_descriptor(0);
  const bool write_audio = audio_track_desc && audio_track_desc->locked;
  const bool write_efm = representation->has_efm();
  const bool write_ac3 = representation->has_ac3_rf();

  const std::string wav_path = base + "_audio_00.wav";
  std::ofstream wav_out;
  uint32_t audio_data_bytes = 0;
  if (write_audio) {
    wav_out.open(wav_path, std::ios::binary | std::ios::trunc);
    if (!wav_out) {
      return {false, 0, "Failed to open output file: " + wav_path};
    }
    // Placeholder header; sizes are patched after the frame loop.
    const auto header = make_wav_header(system, 0);
    wav_out.write(reinterpret_cast<const char*>(header.data()),
                  static_cast<std::streamsize>(header.size()));
  }

  const std::string efm_path = base + ".efm";
  std::ofstream efm_out;
  std::vector<ExtensionRow> efm_rows;
  uint64_t efm_offset = 0;
  if (write_efm) {
    efm_out.open(efm_path, std::ios::binary | std::ios::trunc);
    if (!efm_out) {
      return {false, 0, "Failed to open output file: " + efm_path};
    }
  }

  const std::string ac3_path = base + ".ac3";
  std::ofstream ac3_out;
  std::vector<ExtensionRow> ac3_rows;
  uint64_t ac3_offset = 0;
  if (write_ac3) {
    ac3_out.open(ac3_path, std::ios::binary | std::ios::trunc);
    if (!ac3_out) {
      return {false, 0, "Failed to open output file: " + ac3_path};
    }
  }

  ORC_LOG_DEBUG("CVBSSinkDeps: Writing {} frames to {} ({}, {})", total_frames,
                primary_path, config.signal_type,
                cvbs_sample_encoding_name(config.sample_encoding));

  // --- Frame loop ---
  std::vector<DropoutRow> dropout_rows;
  std::vector<uint16_t> encode_buffer;
  uint64_t frames_written = 0;

  // Encode one flat frame plane and append it to the stream.
  const auto write_plane =
      [&](std::ofstream& out,
          const VideoFrameRepresentation::sample_type* samples, size_t count) {
        encode_buffer.resize(count);
        for (size_t i = 0; i < count; ++i) {
          encode_buffer[i] = encode_cvbs_u10_sample(
              samples[i], config.sample_encoding, blanking_level);
        }
        out.write(reinterpret_cast<const char*>(encode_buffer.data()),
                  static_cast<std::streamsize>(count * sizeof(uint16_t)));
      };

  for (FrameID fid = frame_rng.first; fid <= frame_rng.last; ++fid) {
    if (cancel_requested_ && cancel_requested_->load()) {
      return {false, frames_written, "Cancelled by user"};
    }

    if (!representation->has_frame(fid)) {
      continue;
    }

    const auto desc = representation->get_frame_descriptor(fid);
    if (!desc.has_value()) {
      ORC_LOG_WARN("CVBSSinkDeps: No descriptor for frame {}, skipping", fid);
      continue;
    }
    const size_t sample_count = desc->samples_total;

    const auto* primary_data = yc ? representation->get_frame_luma(fid)
                                  : representation->get_frame(fid);
    const auto* chroma_data =
        yc ? representation->get_frame_chroma(fid) : nullptr;

    if (!primary_data || sample_count == 0 || (yc && !chroma_data)) {
      ORC_LOG_WARN("CVBSSinkDeps: Empty frame data for frame {}, skipping",
                   fid);
      continue;
    }

    write_plane(primary, primary_data, sample_count);
    if (yc) write_plane(chroma, chroma_data, sample_count);

    if (!primary || (yc && !chroma)) {
      return {false, frames_written,
              "Write error at frame " + std::to_string(fid)};
    }

    // Frames skipped above shift subsequent frames down, so all sidecar
    // rows use the output-file frame index rather than the source FrameID.
    const FrameID out_frame_id = static_cast<FrameID>(frames_written);

    for (const DropoutRun& run : representation->get_dropout_hints(fid)) {
      if (run.sample_count == 0) continue;
      dropout_rows.push_back(DropoutRow{out_frame_id, run.sample_start,
                                        run.sample_count, run.severity});
    }

    if (write_audio) {
      const auto audio = representation->get_audio_samples(0, fid);
      if (!audio.empty()) {
        wav_out.write(
            reinterpret_cast<const char*>(audio.data()),
            static_cast<std::streamsize>(audio.size() * sizeof(int16_t)));
        audio_data_bytes +=
            static_cast<uint32_t>(audio.size() * sizeof(int16_t));
      }
    }

    if (write_efm) {
      const auto efm = representation->get_efm_samples(fid);
      if (!efm.empty()) {
        efm_out.write(reinterpret_cast<const char*>(efm.data()),
                      static_cast<std::streamsize>(efm.size()));
      }
      efm_rows.push_back(ExtensionRow{out_frame_id, efm_offset,
                                      static_cast<uint32_t>(efm.size())});
      efm_offset += efm.size();
    }

    if (write_ac3) {
      const auto ac3 = representation->get_ac3_symbols(fid);
      if (!ac3.empty()) {
        ac3_out.write(reinterpret_cast<const char*>(ac3.data()),
                      static_cast<std::streamsize>(ac3.size()));
      }
      ac3_rows.push_back(ExtensionRow{out_frame_id, ac3_offset,
                                      static_cast<uint32_t>(ac3.size())});
      ac3_offset += ac3.size();
    }

    ++frames_written;

    if (frames_written % 10 == 0 && progress_callback_) {
      progress_callback_(frames_written, total_frames,
                         "Writing CVBS: frame " +
                             std::to_string(frames_written) + "/" +
                             std::to_string(total_frames));
    }
  }

  primary.close();
  if (yc) chroma.close();
  if (write_efm) efm_out.close();
  if (write_ac3) ac3_out.close();

  if (frames_written == 0) {
    return {false, 0, "No frames could be written"};
  }

  // --- Finalise the WAV header with the real data size ---
  if (write_audio) {
    const auto header = make_wav_header(system, audio_data_bytes);
    wav_out.seekp(0, std::ios::beg);
    wav_out.write(reinterpret_cast<const char*>(header.data()),
                  static_cast<std::streamsize>(header.size()));
    wav_out.close();
    if (!wav_out) {
      return {false, frames_written, "Write error in " + wav_path};
    }
  }

  // --- Write the .meta core metadata ---
  std::string err;
  const std::optional<bool> audio_locked_meta =
      write_audio ? std::optional<bool>(true) : std::nullopt;
  if (!write_core_metadata(base + ".meta", system, config, frames_written,
                           black_level_override, has_nonstandard_values,
                           audio_locked_meta, err)) {
    return {false, frames_written, err};
  }

  // --- Write the extension sidecars ---
  if (!dropout_rows.empty() &&
      !write_dropout_sidecar(base + ".dropouts.meta", dropout_rows, err)) {
    return {false, frames_written, err};
  }
  if (write_efm && !write_extension_sidecar(base + ".efm.meta", "efm_frame",
                                            efm_rows, err)) {
    return {false, frames_written, err};
  }
  if (write_ac3 && !write_extension_sidecar(base + ".ac3.meta", "ac3_frame",
                                            ac3_rows, err)) {
    return {false, frames_written, err};
  }

  ORC_LOG_INFO(
      "CVBSSinkDeps: Wrote {} frames ({} requested) to {} "
      "(dropouts {}, audio {}, EFM {}, AC3 {})",
      frames_written, total_frames, primary_path,
      dropout_rows.empty() ? "no" : "yes", write_audio ? "yes" : "no",
      write_efm ? "yes" : "no", write_ac3 ? "yes" : "no");

  return {true, frames_written,
          "Success: " + std::to_string(frames_written) + " frames written"};
}

}  // namespace orc
