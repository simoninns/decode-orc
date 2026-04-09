/*
 * File:        nn_ntsc_chroma_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     NN NTSC Chroma Sink Stage implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "nn_ntsc_chroma_sink_stage.h"
#include "nn_output_backend.h"
#include "vfr_to_source_field_adapter.h"
#include "decoders/ntscdecoder.h"
#include "decoders/componentframe.h"
#include "decoders/comb.h"
#include "colour_preview_conversion.h"
#include "logging.h"
#include <common_types.h>
#include <stage_registry.h>
#include "../../analysis/vectorscope/vectorscope_analysis.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace orc {

// Register this stage with the registry
ORC_REGISTER_STAGE(NnNtscChromaSinkStage)

// Force linker to include this object file
void force_link_NnNtscChromaSinkStage() {}

// Non-trivial destructor: preview cache holds unique_ptr to NtscDecoder (incomplete in header)
NnNtscChromaSinkStage::~NnNtscChromaSinkStage() = default;

NnNtscChromaSinkStage::NnNtscChromaSinkStage()
    : output_path_("")
    , output_format_("rgb")
    , threads_(0)
    , chroma_gain_(1.0)
    , chroma_phase_(0.0)
    , deterministic_output_(false)
    , chroma_nr_(0.0)
    , luma_nr_(0.0)
    , trigger_status_("Ready")
{
}

NodeTypeInfo NnNtscChromaSinkStage::get_node_type_info() const
{
    return NodeTypeInfo{
        NodeType::SINK,
        "nn_ntsc_chroma_sink",
        "NN NTSC Chroma Sink",
        "Neural network NTSC chroma decoder (3D spatial/temporal comb filter). "
        "Outputs raw RGB/YUV/Y4M video. NTSC sources only. Trigger to export.",
        1,  // min_inputs
        1,  // max_inputs
        0,  // min_outputs
        0,  // max_outputs
        VideoFormatCompatibility::NTSC_ONLY,
        SinkCategory::THIRD_PARTY
    };
}

std::vector<ArtifactPtr> NnNtscChromaSinkStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context)
{
    (void)parameters;
    (void)observation_context;

    // Cache the input for use by trigger()
    if (!inputs.empty()) {
        auto vfr = std::dynamic_pointer_cast<VideoFieldRepresentation>(inputs[0]);
        if (!vfr) {
            trigger_status_ = "Error: Input must be a VideoFieldRepresentation";
            ORC_LOG_ERROR("NnNtscChromaSink: Input must be a VideoFieldRepresentation");
            return {};
        }

        // Validate NTSC-only constraint
        auto video_params = vfr->get_video_parameters();
        if (video_params && video_params->system != VideoSystem::NTSC) {
            trigger_status_ = "Error: NN NTSC Chroma Sink requires an NTSC source";
            ORC_LOG_ERROR("NnNtscChromaSink: Input is not NTSC (system={})",
                         static_cast<int>(video_params->system));
            return {};
        }

        {
            std::lock_guard<std::mutex> lock(cached_input_mutex_);
            cached_input_ = vfr;
        }
    }
    return {};
}

std::vector<ParameterDescriptor> NnNtscChromaSinkStage::get_parameter_descriptors(
    VideoSystem project_format, SourceType source_type) const
{
    (void)project_format;
    (void)source_type;

    std::vector<ParameterDescriptor> descriptors;

    // output_path
    {
        ParameterDescriptor desc;
        desc.name = "output_path";
        desc.display_name = "Output File";
        desc.description = "Path to output video file (.rgb, .yuv, or .y4m)";
        desc.type = ParameterType::FILE_PATH;
        desc.constraints.required = true;
        desc.constraints.default_value = std::string("");
        desc.file_extension_hint = ".rgb|.yuv|.y4m";
        descriptors.push_back(desc);
    }

    // output_format
    {
        ParameterDescriptor desc;
        desc.name = "output_format";
        desc.display_name = "Output Format";
        desc.description = "Output format:\n"
                           "  rgb  - RGB48 (16-bit per channel, planar)\n"
                           "  yuv  - YUV444P16 (16-bit per channel, planar)\n"
                           "  y4m  - YUV444P16 with Y4M headers";
        desc.type = ParameterType::STRING;
        desc.constraints.default_value = std::string("rgb");
        desc.constraints.allowed_strings = {"rgb", "yuv", "y4m"};
        descriptors.push_back(desc);
    }

    // threads
    {
        ParameterDescriptor desc;
        desc.name = "threads";
        desc.display_name = "Worker Threads";
        desc.description = "Number of worker threads. 0 = auto-detect (capped at 4 to avoid system saturation).";
        desc.type = ParameterType::INT32;
        desc.constraints.default_value = int32_t(0);
        desc.constraints.min_value = int32_t(0);
        desc.constraints.max_value = int32_t(64);
        descriptors.push_back(desc);
    }

    // chroma_gain
    {
        ParameterDescriptor desc;
        desc.name = "chroma_gain";
        desc.display_name = "Chroma Gain";
        desc.description = "Chroma gain factor applied post-decode (0.0 – 10.0).";
        desc.type = ParameterType::DOUBLE;
        desc.constraints.default_value = double(1.0);
        desc.constraints.min_value = double(0.0);
        desc.constraints.max_value = double(10.0);
        descriptors.push_back(desc);
    }

    // chroma_phase
    {
        ParameterDescriptor desc;
        desc.name = "chroma_phase";
        desc.display_name = "Chroma Phase";
        desc.description = "Chroma phase rotation in degrees (-180 to 180) applied during IQ→UV "
                           "conversion.";
        desc.type = ParameterType::DOUBLE;
        desc.constraints.default_value = double(0.0);
        desc.constraints.min_value = double(-180.0);
        desc.constraints.max_value = double(180.0);
        descriptors.push_back(desc);
    }

    // deterministic_output
    {
        ParameterDescriptor desc;
        desc.name = "deterministic_output";
        desc.display_name = "Deterministic Output";
        desc.description = "Forces single-threaded processing for reproducible bit-identical output. "
                           "Overrides the threads parameter.";
        desc.type = ParameterType::BOOL;
        desc.constraints.default_value = bool(false);
        descriptors.push_back(desc);
    }

    // chroma_nr
    {
        ParameterDescriptor desc;
        desc.name = "chroma_nr";
        desc.display_name = "Chroma Noise Reduction";
        desc.description = "Chroma noise reduction level (0.0 = off).";
        desc.type = ParameterType::DOUBLE;
        desc.constraints.default_value = double(0.0);
        desc.constraints.min_value = double(0.0);
        desc.constraints.max_value = double(100.0);
        descriptors.push_back(desc);
    }

    // luma_nr
    {
        ParameterDescriptor desc;
        desc.name = "luma_nr";
        desc.display_name = "Luma Noise Reduction";
        desc.description = "Luma noise reduction level (0.0 = off).";
        desc.type = ParameterType::DOUBLE;
        desc.constraints.default_value = double(0.0);
        desc.constraints.min_value = double(0.0);
        desc.constraints.max_value = double(100.0);
        descriptors.push_back(desc);
    }

    return descriptors;
}

std::map<std::string, ParameterValue> NnNtscChromaSinkStage::get_parameters() const
{
    std::map<std::string, ParameterValue> params;
    params["output_path"]          = output_path_;
    params["output_format"]        = output_format_;
    params["threads"]              = threads_;
    params["chroma_gain"]          = chroma_gain_;
    params["chroma_phase"]         = chroma_phase_;
    params["deterministic_output"] = deterministic_output_;
    params["chroma_nr"]            = chroma_nr_;
    params["luma_nr"]              = luma_nr_;
    return params;
}

bool NnNtscChromaSinkStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    static const std::array<std::string, 8> known_keys{
        "output_path", "output_format", "threads", "chroma_gain",
        "chroma_phase", "deterministic_output", "chroma_nr", "luma_nr"
    };

    // Reject unknown keys
    for (const auto& [key, _] : params) {
        bool found = false;
        for (const auto& k : known_keys) {
            if (k == key) { found = true; break; }
        }
        if (!found) {
            ORC_LOG_ERROR("NnNtscChromaSink: Unknown parameter '{}'", key);
            return false;
        }
    }

    // Validate output_format
    auto it_fmt = params.find("output_format");
    if (it_fmt != params.end()) {
        if (!std::holds_alternative<std::string>(it_fmt->second)) {
            ORC_LOG_ERROR("NnNtscChromaSink: output_format must be a string");
            return false;
        }
        const auto& fmt = std::get<std::string>(it_fmt->second);
        if (fmt != "rgb" && fmt != "yuv" && fmt != "y4m") {
            ORC_LOG_ERROR("NnNtscChromaSink: Invalid output_format '{}' — must be rgb, yuv, or y4m",
                         fmt);
            return false;
        }
    }

    // Apply parameters
    for (const auto& [key, value] : params) {
        if (key == "output_path") {
            if (std::holds_alternative<std::string>(value))
                output_path_ = std::get<std::string>(value);
        } else if (key == "output_format") {
            output_format_ = std::get<std::string>(value);
        } else if (key == "threads") {
            if (std::holds_alternative<int32_t>(value))
                threads_ = std::get<int32_t>(value);
        } else if (key == "chroma_gain") {
            if (std::holds_alternative<double>(value))
                chroma_gain_ = std::get<double>(value);
        } else if (key == "chroma_phase") {
            if (std::holds_alternative<double>(value))
                chroma_phase_ = std::get<double>(value);
        } else if (key == "deterministic_output") {
            if (std::holds_alternative<bool>(value))
                deterministic_output_ = std::get<bool>(value);
        } else if (key == "chroma_nr") {
            if (std::holds_alternative<double>(value))
                chroma_nr_ = std::get<double>(value);
        } else if (key == "luma_nr") {
            if (std::holds_alternative<double>(value))
                luma_nr_ = std::get<double>(value);
        }
    }

    return true;
}

bool NnNtscChromaSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    IObservationContext& observation_context)
{
    (void)observation_context;

    ORC_LOG_DEBUG("NnNtscChromaSink: trigger() called");

    is_processing_.store(true);
    cancel_requested_.store(false);

    // Apply any parameter overrides from the trigger call site
    set_parameters(parameters);

    // --- Validate output path -------------------------------------------------
    if (output_path_.empty()) {
        ORC_LOG_ERROR("NnNtscChromaSink: No output path specified");
        trigger_status_ = "Error: No output path specified";
        is_processing_.store(false);
        return false;
    }

    // --- Resolve VFR ----------------------------------------------------------
    // Prefer the live input supplied to trigger(); fall back to the cached
    // input stored by execute() if the caller passes an empty list.
    std::shared_ptr<const VideoFieldRepresentation> vfr;
    if (!inputs.empty()) {
        vfr = std::dynamic_pointer_cast<const VideoFieldRepresentation>(inputs[0]);
    }
    if (!vfr) {
        std::lock_guard<std::mutex> lock(cached_input_mutex_);
        vfr = cached_input_;
    }
    if (!vfr) {
        ORC_LOG_ERROR("NnNtscChromaSink: No input available");
        trigger_status_ = "Error: No input — run the pipeline first";
        is_processing_.store(false);
        return false;
    }

    // --- Validate NTSC source -------------------------------------------------
    auto video_params_opt = vfr->get_video_parameters();
    if (!video_params_opt) {
        ORC_LOG_ERROR("NnNtscChromaSink: Input has no video parameters");
        trigger_status_ = "Error: No video parameters";
        is_processing_.store(false);
        return false;
    }
    if (video_params_opt->system != VideoSystem::NTSC) {
        ORC_LOG_ERROR("NnNtscChromaSink: Input is not NTSC (system={})",
                     static_cast<int>(video_params_opt->system));
        trigger_status_ = "Error: NN NTSC Chroma Sink requires an NTSC source";
        is_processing_.store(false);
        return false;
    }

    // --- Extract NN video parameters ------------------------------------------
    auto nn_params_opt = VfrToSourceFieldAdapter::extract_video_parameters(*vfr);
    if (!nn_params_opt) {
        ORC_LOG_ERROR("NnNtscChromaSink: Failed to extract NN video parameters");
        trigger_status_ = "Error: Failed to extract video parameters";
        is_processing_.store(false);
        return false;
    }
    nn_ntsc::NnVideoParameters nn_params = *nn_params_opt;

    // Apply active-line hint override (preferred over metadata defaults)
    auto active_line_hint = vfr->get_active_line_hint();
    if (active_line_hint && active_line_hint->is_valid()) {
        nn_params.firstActiveFrameLine = active_line_hint->first_active_frame_line;
        nn_params.lastActiveFrameLine  = active_line_hint->last_active_frame_line;
        ORC_LOG_DEBUG("NnNtscChromaSink: Using active line hint: frame first={}, last={}",
                     active_line_hint->first_active_frame_line,
                     active_line_hint->last_active_frame_line);
    }

    ORC_LOG_DEBUG("NnNtscChromaSink: Video parameters: {}x{}, "
                 "active video {}–{}, active lines {}–{}",
                 nn_params.fieldWidth, nn_params.fieldHeight,
                 nn_params.activeVideoStart, nn_params.activeVideoEnd,
                 nn_params.firstActiveFrameLine, nn_params.lastActiveFrameLine);

    // --- Create output backend (opens file, writes stream header, pads nn_params) --
    NnRawOutputBackend backend;
    {
        NnOutputBackend::Configuration bcfg;
        bcfg.output_path    = output_path_;
        bcfg.format         = output_format_;
        bcfg.padding_amount = 8;
        bcfg.video_params   = &nn_params;

        // initialize() calls updateConfiguration() which pads nn_params in place;
        // the padded values must be used when configuring the per-thread NtscDecoder.
        if (!backend.initialize(bcfg)) {
            trigger_status_ = "Error: Cannot open output file";
            is_processing_.store(false);
            return false;
        }
    }

    // --- Build Comb::Configuration --------------------------------------------
    nn_ntsc::Comb::Configuration comb_cfg;
    comb_cfg.chromaGain        = chroma_gain_;
    comb_cfg.chromaPhase       = chroma_phase_;
    comb_cfg.cNRLevel          = chroma_nr_;
    comb_cfg.yNRLevel          = luma_nr_;
    comb_cfg.dimensions        = 3;     // Always NN 3D mode
    comb_cfg.adaptive          = true;
    comb_cfg.phaseCompensation = false;
    comb_cfg.showMap           = false;

    // Look-behind/ahead from comb config (3D: 1 behind, 2 ahead)
    const int32_t look_behind = comb_cfg.getLookBehind();
    const int32_t look_ahead  = comb_cfg.getLookAhead();

    // --- Resolve thread count -------------------------------------------------
    int32_t num_threads = threads_;
    if (deterministic_output_) {
        num_threads = 1;
    } else if (num_threads <= 0) {
        // Cap auto-detect at 4: nnNtsc ONNX inference is CPU-intensive and
        // spawning hardware_concurrency() workers saturates the machine,
        // causing Linux/GNOME to hang on high-core-count systems.
        constexpr int32_t kAutoThreadCap = 4;
        const int32_t hw = static_cast<int32_t>(std::thread::hardware_concurrency());
        num_threads = (hw > 0) ? std::min(hw, kAutoThreadCap) : kAutoThreadCap;
    }

    // --- Determine frame range ------------------------------------------------
    FieldIDRange field_range     = vfr->field_range();
    size_t total_source_fields   = vfr->field_count();
    size_t total_source_frames   = total_source_fields / 2;

    // Convert inclusive-exclusive field range to 0-based frame numbers
    size_t  start_frame      = field_range.start.value() / 2;
    size_t  end_frame        = field_range.end.value()   / 2;  // exclusive
    int32_t num_output_frames = static_cast<int32_t>(end_frame - start_frame);

    if (num_output_frames <= 0) {
        ORC_LOG_WARN("NnNtscChromaSink: No frames to decode (field range {}-{})",
                    field_range.start.value(), field_range.end.value());
        trigger_status_ = "Warning: No frames to decode";
        is_processing_.store(false);
        return true;
    }

    num_threads = std::min(num_threads, num_output_frames);

    ORC_LOG_DEBUG("NnNtscChromaSink: Decoding {} frames (source frames 0–{}) "
                 "with {} thread(s); look-behind={} look-ahead={}",
                 num_output_frames, total_source_frames - 1,
                 num_threads, look_behind, look_ahead);

    // --- Build field-info list (field IDs + blank flags; no data loaded yet) --
    // Extended range: [start_frame - look_behind ... end_frame + look_ahead)
    int32_t ext_start = static_cast<int32_t>(start_frame) - look_behind;
    int32_t ext_end   = static_cast<int32_t>(end_frame)   + look_ahead;

    struct FieldInfo {
        FieldID field_id;
        bool    use_blank;
    };
    std::vector<FieldInfo> field_info_list;
    field_info_list.reserve(static_cast<size_t>((ext_end - ext_start) * 2));

    for (int32_t frame = ext_start; frame < ext_end; frame++) {
        bool use_blank = (frame < 0 ||
                          frame >= static_cast<int32_t>(total_source_frames));

        // Default field IDs (1-based frame → 0-based field index)
        int32_t meta_frame = use_blank ? 1 : (frame + 1);
        FieldID first_id   = FieldID(static_cast<size_t>((meta_frame * 2) - 2));
        FieldID second_id  = FieldID(static_cast<size_t>((meta_frame * 2) - 1));

        if (!use_blank) {
            // Scan for the top (first) field of this frame, handling any
            // field ordering irregularities from the source.
            FieldID scan = first_id;
            for (int s = 0; s < 10 && scan.value() < field_range.end.value(); s++) {
                if (!vfr->has_field(scan)) {
                    scan = FieldID(scan.value() + 1);
                    continue;
                }
                auto desc = vfr->get_descriptor(scan);
                if (desc && desc->parity == FieldParity::Top) {
                    first_id  = scan;
                    second_id = FieldID(scan.value() + 1);
                    break;
                }
                scan = FieldID(scan.value() + 1);
            }
            if (!vfr->has_field(first_id) || !vfr->has_field(second_id)) {
                ORC_LOG_WARN("NnNtscChromaSink: Frame {} missing fields ({}/{}); "
                            "substituting black", frame + 1,
                            first_id.value(), second_id.value());
                use_blank = true;
            }
        }

        field_info_list.push_back({first_id,  use_blank});
        field_info_list.push_back({second_id, use_blank});
    }

    // --- Parallel decode + sequential write -----------------------------------
    // output_frames[i] is set by the worker that decodes output frame i.
    // The sequential writer drains completed frames in order under write_mutex.
    std::vector<std::optional<nn_ntsc::ComponentFrame>> output_frames(
        static_cast<size_t>(num_output_frames));

    std::atomic<int32_t> next_frame_idx{0};
    int32_t              next_to_write = 0;  // protected by write_mutex
    std::atomic<bool>    abort_flag{false};
    std::mutex           write_mutex;
    bool                 write_error = false;

    const int32_t required_lb_fields = look_behind * 2;

    if (progress_callback_) {
        progress_callback_(0, num_output_frames, "Starting NN chroma decode...");
    }

    // Worker lambda — each thread owns its own NtscDecoder instance.
    // The FFTW plans and ONNX session inside Comb are static singletons
    // initialised exactly once via std::call_once (thread-safe).
    auto worker_func = [&]() {
        nn_ntsc::NtscDecoder decoder(comb_cfg);
        decoder.configure(nn_params);

        while (!abort_flag.load()) {
            if (cancel_requested_.load()) {
                abort_flag.store(true);
                break;
            }

            const int32_t frame_idx = next_frame_idx.fetch_add(1);
            if (frame_idx >= num_output_frames) break;

            // Frame's position in field_info_list
            const int32_t actual_frame      = static_cast<int32_t>(start_frame) + frame_idx;
            const int32_t frame_list_start  = (actual_frame - ext_start) * 2;

            // Window of field_info to load for this frame
            int32_t copy_start = frame_list_start - required_lb_fields;
            int32_t copy_end   = frame_list_start + 2 + (look_ahead * 2);
            copy_start = std::max(0, copy_start);
            copy_end   = std::min(static_cast<int32_t>(field_info_list.size()), copy_end);

            // Build NnSourceField window (load data on-demand from VFR)
            std::vector<nn_ntsc::NnSourceField> frame_fields;
            frame_fields.reserve(static_cast<size_t>(copy_end - copy_start));

            auto make_blank_field = [&](size_t seq) -> nn_ntsc::NnSourceField {
                nn_ntsc::NnSourceField blank;
                blank.seq_no         = static_cast<int32_t>(seq);
                blank.is_first_field = (seq % 2 == 0);
                blank.field_phase_id = 0;
                blank.data.assign(
                    static_cast<size_t>(nn_params.fieldWidth) *
                    static_cast<size_t>(nn_params.fieldHeight),
                    static_cast<uint16_t>(nn_params.black16bIre));
                return blank;
            };

            for (int32_t i = copy_start; i < copy_end; i++) {
                const auto& fi = field_info_list[static_cast<size_t>(i)];
                if (fi.use_blank) {
                    frame_fields.push_back(make_blank_field(fi.field_id.value()));
                } else {
                    auto sf_opt = VfrToSourceFieldAdapter::convert(*vfr, fi.field_id);
                    if (sf_opt) {
                        frame_fields.push_back(std::move(*sf_opt));
                    } else {
                        frame_fields.push_back(make_blank_field(fi.field_id.value()));
                    }
                }
            }

            // Determine where in frame_fields the target frame starts.
            // If we couldn't fill the full lookbehind (boundary), pad with
            // black fields at the front to keep startIndex consistent.
            const int32_t actual_lb = frame_list_start - copy_start;
            if (actual_lb < required_lb_fields) {
                const int32_t pad_count = required_lb_fields - actual_lb;
                std::vector<nn_ntsc::NnSourceField> padded;
                padded.reserve(static_cast<size_t>(pad_count) + frame_fields.size());
                for (int32_t p = 0; p < pad_count; p++) {
                    padded.push_back(make_blank_field(static_cast<size_t>(p)));
                }
                for (auto& f : frame_fields) padded.push_back(std::move(f));
                frame_fields = std::move(padded);
            }

            const int32_t start_idx = required_lb_fields;      // target frame first field
            const int32_t end_idx   = start_idx + 2;            // exclusive (one frame)

            // Decode this frame
            std::vector<nn_ntsc::ComponentFrame> comp_frames(1);
            decoder.decodeFrames(frame_fields, start_idx, end_idx, comp_frames);

            // Store result and drain any consecutive completed frames to disk
            {
                std::lock_guard<std::mutex> lock(write_mutex);
                output_frames[static_cast<size_t>(frame_idx)] = std::move(comp_frames[0]);

                // Sequential drain: write as many in-order frames as are ready
                while (next_to_write < num_output_frames &&
                       output_frames[static_cast<size_t>(next_to_write)].has_value()) {

                    if (!backend.writeFrame(output_frames[static_cast<size_t>(next_to_write)].value())) {
                        ORC_LOG_ERROR("NnNtscChromaSink: File write error at frame {}",
                                     next_to_write + 1);
                        write_error = true;
                        abort_flag.store(true);
                    }

                    // Report progress every 10 frames (or the final frame)
                    const int32_t done = next_to_write + 1;
                    if (progress_callback_ &&
                        (done % 10 == 0 || done == num_output_frames)) {
                        progress_callback_(
                            done, num_output_frames,
                            "NN decoding: " + std::to_string(done) + "/" +
                            std::to_string(num_output_frames) + " frames");
                    }

                    // Free memory and advance
                    output_frames[static_cast<size_t>(next_to_write)] = std::nullopt;
                    next_to_write++;
                }
            }

            if (abort_flag.load()) break;
        }
    };

    // Launch worker threads
    if (num_threads == 1) {
        // Deterministic / single-threaded path — run directly on calling thread
        worker_func();
    } else {
        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(num_threads));
        for (int32_t t = 0; t < num_threads; t++) {
            workers.emplace_back(worker_func);
        }
        for (auto& w : workers) w.join();
    }

    backend.finalize();

    if (cancel_requested_.load()) {
        trigger_status_ = "Cancelled";
        ORC_LOG_INFO("NnNtscChromaSink: Decode cancelled after {} frames written",
                    next_to_write);
        is_processing_.store(false);
        return false;
    }

    if (write_error) {
        trigger_status_ = "Error: File write failure";
        is_processing_.store(false);
        return false;
    }

    trigger_status_ = "Complete — " + std::to_string(num_output_frames) +
                      " frames written to " + output_path_;
    ORC_LOG_INFO("NnNtscChromaSink: Decode complete — {} frames → '{}'",
                num_output_frames, output_path_);

    if (progress_callback_) {
        progress_callback_(num_output_frames, num_output_frames,
                          "NN decode complete");
    }

    is_processing_.store(false);
    return true;
}

std::string NnNtscChromaSinkStage::get_trigger_status() const
{
    return trigger_status_;
}

// =============================================================================
// PreviewableStage interface
// =============================================================================

std::vector<PreviewOption> NnNtscChromaSinkStage::get_preview_options() const
{
    std::shared_ptr<const VideoFieldRepresentation> local_input;
    {
        std::lock_guard<std::mutex> lock(cached_input_mutex_);
        local_input = cached_input_;
    }

    if (!local_input) return {};

    auto video_params = local_input->get_video_parameters();
    if (!video_params) return {};

    if (video_params->system != VideoSystem::NTSC) return {};

    const uint64_t field_count = local_input->field_count();
    if (field_count < 2) return {};

    const uint64_t frame_count = field_count / 2;

    // Compute full decoded frame dimensions directly from the video parameters.
    // This avoids running a full ONNX inference just to discover the frame size.
    uint32_t full_width  = video_params->field_width  > 0
        ? static_cast<uint32_t>(video_params->field_width)  : 910u;
    uint32_t full_height = video_params->field_height > 0
        ? static_cast<uint32_t>((video_params->field_height * 2) - 1) : 505u;

    // Active picture area for DAR correction
    uint32_t active_width  = 758u;  // Typical NTSC active picture width
    uint32_t active_height = 486u;  // Typical NTSC active picture height
    if (video_params->active_video_start >= 0
        && video_params->active_video_end > video_params->active_video_start) {
        active_width = static_cast<uint32_t>(
            video_params->active_video_end - video_params->active_video_start);
    }
    if (video_params->first_active_frame_line >= 0
        && video_params->last_active_frame_line > video_params->first_active_frame_line) {
        active_height = static_cast<uint32_t>(
            video_params->last_active_frame_line - video_params->first_active_frame_line);
    }

    const double active_ratio  = static_cast<double>(active_width) / static_cast<double>(active_height);
    const double dar_correction = (4.0 / 3.0) / active_ratio;

    std::vector<PreviewOption> options;
    options.push_back(PreviewOption{
        "frame", "Frame (RGB)", false, full_width, full_height, frame_count, dar_correction
    });
    return options;
}

PreviewImage NnNtscChromaSinkStage::render_preview(
    const std::string& option_id,
    uint64_t           index,
    PreviewNavigationHint hint) const
{
    if (option_id != "frame") return PreviewImage{};

    auto carrier_opt = get_colour_preview_carrier(index, hint);
    if (!carrier_opt.has_value()) return PreviewImage{};

    auto result = render_preview_from_colour_carrier(*carrier_opt);
    result.vectorscope_data = carrier_opt->vectorscope_data;
    return result;
}

// =============================================================================
// IStagePreviewCapability interface
// =============================================================================

StagePreviewCapability NnNtscChromaSinkStage::get_preview_capability() const
{
    StagePreviewCapability capability{};

    // Declare tweakable parameters unconditionally so the GUI can build
    // the live-tweak panel as soon as the node is selected — even before data
    // is loaded.
    capability.tweakable_parameters.push_back({"chroma_gain",  PreviewTweakClass::DecodePhase});
    capability.tweakable_parameters.push_back({"chroma_phase", PreviewTweakClass::DecodePhase});
    capability.tweakable_parameters.push_back({"luma_nr",      PreviewTweakClass::DecodePhase});
    capability.tweakable_parameters.push_back({"chroma_nr",    PreviewTweakClass::DecodePhase});

    std::shared_ptr<const VideoFieldRepresentation> local_input;
    {
        std::lock_guard<std::mutex> lock(cached_input_mutex_);
        local_input = cached_input_;
    }

    if (!local_input) return capability;  // tweakable params declared; geometry not yet valid

    auto video_params_opt = local_input->get_video_parameters();
    if (!video_params_opt) return capability;

    const auto& video_params = *video_params_opt;
    if (video_params.system != VideoSystem::NTSC) return capability;

    capability.supported_data_types.push_back(VideoDataType::ColourNTSC);
    // Also advertise the input signal type so the preview infrastructure can
    // resolve the upstream VFR and expose input-side views.
    capability.supported_data_types.push_back(
        local_input->has_separate_channels()
            ? VideoDataType::YC_NTSC
            : VideoDataType::CompositeNTSC);

    const uint64_t field_count = local_input->field_count();
    capability.navigation_extent.item_count  = field_count / 2;
    capability.navigation_extent.granularity = 1;
    capability.navigation_extent.item_label  = "frame";

    // Active picture geometry (used for DAR correction and export sizing)
    capability.geometry.active_width  = 758u;
    capability.geometry.active_height = 486u;
    if (video_params.active_video_start >= 0
        && video_params.active_video_end > video_params.active_video_start) {
        capability.geometry.active_width = static_cast<uint32_t>(
            video_params.active_video_end - video_params.active_video_start);
    }
    if (video_params.first_active_frame_line >= 0
        && video_params.last_active_frame_line > video_params.first_active_frame_line) {
        capability.geometry.active_height = static_cast<uint32_t>(
            video_params.last_active_frame_line - video_params.first_active_frame_line);
    }
    if (capability.geometry.active_height == 0) {
        capability.geometry.active_height = 486u;
    }

    capability.geometry.display_aspect_ratio = 4.0 / 3.0;
    const double active_ratio = static_cast<double>(capability.geometry.active_width)
                              / static_cast<double>(capability.geometry.active_height);
    capability.geometry.dar_correction_factor = (4.0 / 3.0) / active_ratio;

    return capability;
}

// =============================================================================
// IColourPreviewProvider interface
// =============================================================================

std::optional<ColourFrameCarrier> NnNtscChromaSinkStage::get_colour_preview_carrier(
    uint64_t              frame_index,
    PreviewNavigationHint hint [[maybe_unused]]) const
{
    ORC_LOG_DEBUG("NnNtscChromaSink: get_colour_preview_carrier called for frame {}",
                  frame_index);

    std::shared_ptr<const VideoFieldRepresentation> local_input;
    {
        std::lock_guard<std::mutex> lock(cached_input_mutex_);
        local_input = cached_input_;
    }

    if (!local_input) return std::nullopt;

    auto video_params_opt = local_input->get_video_parameters();
    if (!video_params_opt) return std::nullopt;

    const SourceParameters& videoParams = *video_params_opt;
    if (videoParams.system != VideoSystem::NTSC) return std::nullopt;

    // Extract NN video parameters (no output padding needed for preview)
    auto nn_params_opt = VfrToSourceFieldAdapter::extract_video_parameters(*local_input);
    if (!nn_params_opt) return std::nullopt;
    nn_ntsc::NnVideoParameters nn_params = *nn_params_opt;

    // Apply active-line hint override if available
    auto active_line_hint = local_input->get_active_line_hint();
    if (active_line_hint && active_line_hint->is_valid()) {
        nn_params.firstActiveFrameLine = active_line_hint->first_active_frame_line;
        nn_params.lastActiveFrameLine  = active_line_hint->last_active_frame_line;
    }

    // Determine first-field offset (some VFRs start on the bottom field)
    uint64_t first_field_offset = 0;
    auto parity_hint = local_input->get_field_parity_hint(FieldID(0));
    if (parity_hint.has_value() && !parity_hint->is_first_field) {
        first_field_offset = 1;
    }

    const uint64_t field_a_index = first_field_offset + (frame_index * 2);
    const uint64_t field_b_index = field_a_index + 1;
    const uint64_t total_fields  = local_input->field_count();

    // NN 3D mode: look-behind = 1 frame (2 fields), look-ahead = 2 frames (4 fields)
    constexpr int32_t kLookBehindFields = 2;
    constexpr int32_t kLookAheadFields  = 4;

    const int64_t start_field = static_cast<int64_t>(field_a_index) - kLookBehindFields;
    const int64_t end_field   = static_cast<int64_t>(field_b_index) + kLookAheadFields;

    // Helper: build a black blank field with correct seq_no and field parity
    auto make_blank_field = [&](int64_t idx) -> nn_ntsc::NnSourceField {
        nn_ntsc::NnSourceField blank;
        blank.seq_no         = static_cast<int32_t>(idx);
        blank.is_first_field = (idx % 2 == 0);
        blank.field_phase_id = 0;
        blank.data.assign(
            static_cast<size_t>(nn_params.fieldWidth) *
            static_cast<size_t>(nn_params.fieldHeight),
            static_cast<uint16_t>(nn_params.black16bIre));
        return blank;
    };

    // Build the NnSourceField window for this frame
    std::vector<nn_ntsc::NnSourceField> input_fields;
    input_fields.reserve(static_cast<size_t>(kLookBehindFields + 2 + kLookAheadFields));

    for (int64_t f = start_field; f <= end_field; ++f) {
        if (f >= 0 && f < static_cast<int64_t>(total_fields) &&
            local_input->has_field(FieldID(static_cast<size_t>(f)))) {
            auto sf_opt = VfrToSourceFieldAdapter::convert(
                *local_input, FieldID(static_cast<size_t>(f)));
            input_fields.push_back(sf_opt ? std::move(*sf_opt) : make_blank_field(f));
        } else {
            input_fields.push_back(make_blank_field(f));
        }
    }

    if (input_fields.size() < 2) return std::nullopt;

    // Build Comb::Configuration from the current stage parameters
    nn_ntsc::Comb::Configuration comb_cfg;
    comb_cfg.chromaGain        = chroma_gain_;
    comb_cfg.chromaPhase       = chroma_phase_;
    comb_cfg.cNRLevel          = chroma_nr_;
    comb_cfg.yNRLevel          = luma_nr_;
    comb_cfg.dimensions        = 3;
    comb_cfg.adaptive          = true;
    comb_cfg.phaseCompensation = false;
    comb_cfg.showMap           = false;

    // Rebuild the preview decoder only when decode parameters change.
    // The static FFTW plans and ONNX session inside Comb are created exactly
    // once via std::call_once, so rebuilding the NtscDecoder is cheap — only
    // Comb::updateConfiguration() runs again.
    if (!preview_decoder_cache_.matches_config(
            chroma_gain_, chroma_phase_, chroma_nr_, luma_nr_)) {
        preview_decoder_cache_.decoder = std::make_unique<nn_ntsc::NtscDecoder>(comb_cfg);
        preview_decoder_cache_.decoder->configure(nn_params);
        preview_decoder_cache_.chroma_gain  = chroma_gain_;
        preview_decoder_cache_.chroma_phase = chroma_phase_;
        preview_decoder_cache_.chroma_nr    = chroma_nr_;
        preview_decoder_cache_.luma_nr      = luma_nr_;
    }

    // Decode one frame: startIndex points at the target frame's first field
    const int32_t start_idx = kLookBehindFields;  // always 2
    const int32_t end_idx   = start_idx + 2;       // exclusive (one frame = 2 fields)

    std::vector<nn_ntsc::ComponentFrame> comp_frames(1);
    preview_decoder_cache_.decoder->decodeFrames(input_fields, start_idx, end_idx, comp_frames);

    const nn_ntsc::ComponentFrame& frame = comp_frames[0];
    const int32_t width  = frame.getWidth();
    const int32_t height = frame.getHeight();
    if (width <= 0 || height <= 0) return std::nullopt;

    // Populate the carrier
    ColourFrameCarrier carrier{};
    carrier.data_type   = VideoDataType::ColourNTSC;
    carrier.colorimetry = ColorimetricMetadata::default_ntsc();
    carrier.system      = VideoSystem::NTSC;
    carrier.frame_index = frame_index;
    carrier.width       = static_cast<uint32_t>(width);
    carrier.height      = static_cast<uint32_t>(height);

    carrier.active_x_start =
        (videoParams.active_video_start >= 0 && videoParams.active_video_start < width)
            ? static_cast<uint32_t>(videoParams.active_video_start) : 0U;
    carrier.active_x_end =
        (videoParams.active_video_end > videoParams.active_video_start
         && videoParams.active_video_end <= width)
            ? static_cast<uint32_t>(videoParams.active_video_end) : carrier.width;
    carrier.active_y_start =
        (videoParams.first_active_frame_line >= 0
         && videoParams.first_active_frame_line < height)
            ? static_cast<uint32_t>(videoParams.first_active_frame_line) : 0U;
    carrier.active_y_end =
        (videoParams.last_active_frame_line > videoParams.first_active_frame_line
         && videoParams.last_active_frame_line <= height)
            ? static_cast<uint32_t>(videoParams.last_active_frame_line) : carrier.height;

    carrier.black_16b_ire = videoParams.black_16b_ire;
    carrier.white_16b_ire = videoParams.white_16b_ire;

    // Copy Y/U/V planes (full frame — renderer crops to active area)
    const size_t samples = static_cast<size_t>(width) * static_cast<size_t>(height);
    carrier.y_plane.reserve(samples);
    carrier.u_plane.reserve(samples);
    carrier.v_plane.reserve(samples);

    for (int32_t y = 0; y < height; ++y) {
        const double* yLine = frame.y(y);
        const double* uLine = frame.u(y);
        const double* vLine = frame.v(y);
        for (int32_t x = 0; x < width; ++x) {
            carrier.y_plane.push_back(yLine[x]);
            carrier.u_plane.push_back(uLine[x]);
            carrier.v_plane.push_back(vLine[x]);
        }
    }

    // Extract vectorscope data from the decoded planes (active area only)
    carrier.vectorscope_data = VectorscopeAnalysisTool::extractFromColourFrameCarrier(
        carrier, field_a_index, 4, true);
    if (carrier.vectorscope_data.has_value()) {
        carrier.vectorscope_data->system        = VideoSystem::NTSC;
        carrier.vectorscope_data->white_16b_ire = videoParams.white_16b_ire;
        carrier.vectorscope_data->black_16b_ire = videoParams.black_16b_ire;
    }

    if (!carrier.is_valid()) return std::nullopt;
    return carrier;
}

// =============================================================================
// Future integration note
// =============================================================================
//
// Once the NN decoder approach is validated on real signals and the output
// quality is confirmed to meet or exceed the conventional ntsc3d comb filter
// on a representative set of sources, the decoder code under
// decoders/comb.h/.cpp can be promoted to:
//
//   orc/core/stages/chroma_sink/decoders/
//
// and wired into ChromaSinkStage as an additional decoder type "nnntsc3d",
// following the same pattern as the existing "ntsc2d" and "ntsc3d" decoders.
// The NtscDecoder wrapper and the VfrToSourceFieldAdapter would move with it.
//
// The present separation into a standalone "Sink (3rd party)" stage is
// intentional: it allows independent validation of the NN approach on
// real-world signals without perturbing existing core decoding workflows or
// requiring changes to the ChromaSinkStage parameter surface until the quality
// bar is confirmed.
//
// When promoting, the key integration points are:
//   - Add "nnntsc3d" to the allowed_strings list in ChromaSinkStage's
//     "decoder" parameter descriptor
//   - Instantiate NtscDecoder (from decoders/ntscdecoder.h) in the
//     ChromaSinkStage trigger alongside the existing Decoder subclasses
//   - The ONNX model embedding (chroma_net_v2_onnx_data.cpp, generated by
//     CMake) can remain in orc/core/CMakeLists.txt and be shared between both
//     stages via the same static singleton initialisation path
//   - The VfrToSourceFieldAdapter can be refactored into a common utility
//     header under orc/core/stages/chroma_sink/ once it is no longer
//     exclusively serving this stage

} // namespace orc
