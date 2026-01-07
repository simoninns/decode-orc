/*
 * File:        ffmpegpresetdialog.cpp
 * Module:      orc-gui
 * Purpose:     Configuration dialog for FFmpeg video sink presets
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "ffmpegpresetdialog.h"
#include <QFormLayout>
#include <QVBoxLayout>
#include <QTextEdit>

FFmpegPresetDialog::FFmpegPresetDialog(QWidget *parent)
    : ConfigDialogBase("FFmpeg Export Preset Configuration", parent),
      updating_ui_(false)
{
    // Initialize preset database
    all_presets_ = {
        // Lossless/Archive
        {"mkv-ffv1", "FFV1 Lossless", 
         "Best for archival storage. Mathematically lossless compression. Large file size but perfect quality preservation. Use for master copies.",
         "mkv", "ffv1", false, false, 0, "medium", 0},
        
        // ProRes (Professional)
        {"mov-prores", "ProRes 422 HQ", 
         "Professional editing codec. Excellent quality, moderate file size. Standard for professional video editing. Compatible with Final Cut Pro, DaVinci Resolve, Adobe Premiere.",
         "mov", "prores", false, false, 0, "medium", 0},
        {"mov-prores_4444", "ProRes 4444", 
         "ProRes with alpha channel support and highest chroma quality. Use when you need the best possible quality for compositing or color grading.",
         "mov", "prores_4444", false, false, 0, "medium", 0},
        {"mov-prores_4444xq", "ProRes 4444 XQ", 
         "Highest quality ProRes variant. Maximum quality for demanding post-production workflows. Very large files.",
         "mov", "prores_4444xq", false, false, 0, "medium", 0},
        {"mov-prores_videotoolbox", "ProRes (Apple Hardware)", 
         "Hardware-accelerated ProRes encoding on Apple Silicon and recent Intel Macs. Fast encoding with excellent quality.",
         "mov", "prores_videotoolbox", true, false, 0, "medium", 0},
        
        // Uncompressed
        {"mov-v210", "V210 (10-bit 4:2:2 Uncompressed)", 
         "Completely uncompressed 10-bit 4:2:2 video. Massive file sizes but zero quality loss. Use for highest-quality mastering.",
         "mov", "v210", false, false, 0, "medium", 0},
        {"mov-v410", "V410 (10-bit 4:4:4 Uncompressed)", 
         "Completely uncompressed 10-bit 4:4:4 video. Even larger than V210 but preserves all chroma information.",
         "mov", "v410", false, false, 0, "medium", 0},
        
        // Broadcast
        {"mxf-mpeg2video", "D10/IMX (Broadcast)", 
         "Sony IMX/XDCAM D10 format for broadcast delivery. MXF container with MPEG-2 intra-frame encoding. Standard for broadcast archives.",
         "mxf", "mpeg2video", false, false, 0, "medium", 50000000},
        
        // H.264 (Universal Playback)
        {"mp4-h264", "H.264 (High Quality)", 
         "Universal playback compatibility. Excellent quality-to-size ratio. Plays on virtually all devices and platforms. Good for archival and sharing.",
         "mp4", "h264", true, true, 18, "slow", 0},
        {"mp4-h264_lossless", "H.264 Lossless", 
         "Mathematically lossless H.264 encoding. Smaller than FFV1 but slower to encode. Good compromise for archival.",
         "mp4", "h264", false, false, 0, "veryslow", 0},
        {"mov-h264", "H.264 in MOV", 
         "H.264 in QuickTime MOV container. Better compatibility with Apple ecosystem and professional tools than MP4.",
         "mov", "h264", true, true, 18, "slow", 0},
        
        // H.265 (Better Compression)
        {"mp4-hevc", "H.265/HEVC (High Quality)", 
         "Next-generation codec with 50% better compression than H.264. Smaller files, same quality. Requires modern devices for playback.",
         "mp4", "hevc", true, true, 23, "slow", 0},
        {"mp4-hevc_lossless", "H.265/HEVC Lossless", 
         "Mathematically lossless H.265 encoding. Better compression than H.264 lossless. Excellent for archival with modern tools.",
         "mp4", "hevc", false, false, 0, "veryslow", 0},
        {"mov-hevc", "H.265/HEVC in MOV", 
         "H.265 in QuickTime MOV container. Better compatibility with Apple ecosystem and professional tools.",
         "mov", "hevc", true, true, 23, "slow", 0},
        
        // AV1 (Modern)
        {"mp4-av1", "AV1 (Web Delivery)", 
         "Modern royalty-free codec. Better compression than H.265. Excellent for web streaming. Limited device support currently.",
         "mp4", "av1", false, true, 24, "medium", 0},
        {"mp4-av1_lossless", "AV1 Lossless", 
         "Mathematically lossless AV1 encoding. Best compression for lossless archival. Slow encoding but excellent results.",
         "mp4", "av1_lossless", false, false, 0, "medium", 0}
    };
    
    // Create category selection group
    auto* category_group = create_group("Export Category");
    auto* category_layout = qobject_cast<QFormLayout*>(category_group->layout());
    
    QStringList categories;
    categories << "Lossless/Archive" 
               << "Professional/ProRes" 
               << "Uncompressed"
               << "Broadcast" 
               << "Universal (H.264)"
               << "Modern (H.265/AV1)"
               << "Hardware Accelerated";
    
    category_combo_ = add_combobox(category_layout, "Category:", categories,
        "Select the export category that best matches your needs");
    
    // Create preset selection group
    auto* preset_group = create_group("Preset Selection");
    auto* preset_layout = qobject_cast<QFormLayout*>(preset_group->layout());
    
    preset_combo_ = add_combobox(preset_layout, "Preset:", QStringList(),
        "Select the specific export preset");
    
    description_label_ = new QLabel();
    description_label_->setWordWrap(true);
    description_label_->setMinimumHeight(80);
    preset_layout->addRow("Description:", description_label_);
    
    // Hardware encoder group (hidden by default)
    hardware_group_ = create_group("Hardware Acceleration");
    auto* hardware_layout = qobject_cast<QFormLayout*>(hardware_group_->layout());
    
    QStringList hw_options;
    hw_options << "Software (libx264/libx265)" << "Auto-detect hardware";
    hardware_encoder_combo_ = add_combobox(hardware_layout, "Encoder:", hw_options,
        "Select hardware or software encoding");
    
    hardware_status_label_ = new QLabel();
    hardware_status_label_->setWordWrap(true);
    hardware_layout->addRow("Status:", hardware_status_label_);
    hardware_group_->setVisible(false);
    
    // Options group
    auto* options_group = create_group("Export Options");
    auto* options_layout = qobject_cast<QFormLayout*>(options_group->layout());
    
    deinterlace_checkbox_ = add_checkbox(options_layout, "Deinterlace for web",
        "Apply deinterlacing filter (bwdif) for progressive web playback. Recommended for H.264/H.265/AV1 web variants.");
    deinterlace_checkbox_->setChecked(false);
    
    embed_audio_checkbox_ = add_checkbox(options_layout, "Embed audio",
        "Include analogue audio tracks from the source (if available)");
    embed_audio_checkbox_->setChecked(false);
    
    embed_captions_checkbox_ = add_checkbox(options_layout, "Embed closed captions",
        "Convert EIA-608 closed captions to subtitle track (MP4/MOV only)");
    embed_captions_checkbox_->setChecked(false);
    
    // Advanced settings group
    auto* advanced_group = create_group("Advanced Settings (Optional)");
    auto* advanced_layout = qobject_cast<QFormLayout*>(advanced_group->layout());
    
    add_info_label(advanced_layout,
        "These settings override the preset defaults. Leave at default unless you have specific requirements.");
    
    QStringList quality_presets;
    quality_presets << "Default (from preset)" << "Fast" << "Medium" << "Slow" << "Very Slow";
    quality_preset_combo_ = add_combobox(advanced_layout, "Encoder Speed:", quality_presets,
        "Encoder speed preset. Slower = better compression/quality at same file size");
    
    crf_spinbox_ = add_spinbox(advanced_layout, "Quality (CRF):", 0, 51, 0,
        "Constant Rate Factor: lower = better quality, larger files. 0 = auto from preset, 18 = visually lossless, 23 = high quality, 28 = medium");
    
    bitrate_spinbox_ = add_spinbox(advanced_layout, "Bitrate (Mbps):", 0, 500, 0,
        "Target bitrate in Mbps. 0 = use CRF mode (recommended). Only needed for specific delivery requirements.");
    
    // Detect available hardware encoders
    detect_available_hardware_encoders();
    
    // Connect signals
    connect(category_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FFmpegPresetDialog::on_category_changed);
    connect(preset_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FFmpegPresetDialog::on_preset_changed);
    connect(hardware_encoder_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FFmpegPresetDialog::on_hardware_encoder_changed);
    connect(deinterlace_checkbox_, &QCheckBox::checkStateChanged,
            this, &FFmpegPresetDialog::on_deinterlace_changed);
    
    // Initialize with first category
    on_category_changed(0);
}

void FFmpegPresetDialog::apply_configuration()
{
    if (current_category_presets_.empty() || preset_combo_->currentIndex() < 0) {
        return;
    }
    
    const auto& preset = current_category_presets_[preset_combo_->currentIndex()];
    
    // Set basic output format (container-codec)
    std::string format_string = preset.container + "-" + preset.codec;
    set_parameter("output_format", format_string);
    
    // Set hardware encoder preference
    std::string hardware_encoder = "none";
    if (preset.supports_hardware && hardware_group_->isVisible() && hardware_encoder_combo_->currentIndex() > 0) {
        if (!available_hw_encoders_.empty()) {
            hardware_encoder = available_hw_encoders_[hardware_encoder_combo_->currentIndex() - 1];  // -1 for "None" option
        }
    }
    set_parameter("hardware_encoder", hardware_encoder);
    
    // Set ProRes profile if applicable
    if (preset.codec == "prores") {
        // Extract profile from format_string (e.g., "mov-prores_hq" -> "hq")
        std::string profile = "hq";  // Default
        size_t underscore_pos = preset.format_string.find('_');
        if (underscore_pos != std::string::npos) {
            profile = preset.format_string.substr(underscore_pos + 1);
        }
        set_parameter("prores_profile", profile);
    }
    
    // Set lossless mode
    bool lossless = (preset.format_string.find("_lossless") != std::string::npos);
    set_parameter("use_lossless_mode", lossless);
    
    // Set deinterlacing
    set_parameter("apply_deinterlace", deinterlace_checkbox_->isChecked());
    
    // Set encoder preset
    std::string encoder_preset = preset.default_preset;
    if (quality_preset_combo_->currentIndex() > 0) {
        const char* presets[] = {"", "fast", "medium", "slow", "veryslow"};
        encoder_preset = presets[quality_preset_combo_->currentIndex()];
    }
    set_parameter("encoder_preset", encoder_preset);
    
    // Set CRF
    int crf = crf_spinbox_->value();
    if (crf == 0) {
        crf = preset.default_crf;
    }
    set_parameter("encoder_crf", crf);
    
    // Set bitrate
    int bitrate_mbps = bitrate_spinbox_->value();
    int bitrate = bitrate_mbps > 0 ? bitrate_mbps * 1000000 : preset.default_bitrate;
    set_parameter("encoder_bitrate", bitrate);
    
    // Set options
    set_parameter("embed_audio", embed_audio_checkbox_->isChecked());
    set_parameter("embed_closed_captions", embed_captions_checkbox_->isChecked());
}

void FFmpegPresetDialog::load_from_parameters(const std::map<std::string, orc::ParameterValue>& params)
{
    updating_ui_ = true;
    
    // Load output format and try to find matching preset
    auto it = params.find("output_format");
    if (it != params.end() && std::holds_alternative<std::string>(it->second)) {
        const std::string& format = std::get<std::string>(it->second);
        
        // Find matching preset
        bool found = false;
        for (size_t i = 0; i < all_presets_.size(); ++i) {
            if (all_presets_[i].format_string == format) {
                // Find which category this preset belongs to
                // For simplicity, just select first category for now
                category_combo_->setCurrentIndex(0);
                update_preset_list();
                
                // Find preset in current category
                for (size_t j = 0; j < current_category_presets_.size(); ++j) {
                    if (current_category_presets_[j].format_string == format) {
                        preset_combo_->setCurrentIndex(j);
                        found = true;
                        break;
                    }
                }
                break;
            }
        }
        
        if (!found) {
            category_combo_->setCurrentIndex(0);
            preset_combo_->setCurrentIndex(0);
        }
    }
    
    // Load encoder preset
    auto preset_it = params.find("encoder_preset");
    if (preset_it != params.end() && std::holds_alternative<std::string>(preset_it->second)) {
        const std::string& preset = std::get<std::string>(preset_it->second);
        if (preset == "fast") quality_preset_combo_->setCurrentIndex(1);
        else if (preset == "medium") quality_preset_combo_->setCurrentIndex(2);
        else if (preset == "slow") quality_preset_combo_->setCurrentIndex(3);
        else if (preset == "veryslow") quality_preset_combo_->setCurrentIndex(4);
        else quality_preset_combo_->setCurrentIndex(0);
    }
    
    // Load CRF
    auto crf_it = params.find("encoder_crf");
    if (crf_it != params.end() && std::holds_alternative<int>(crf_it->second)) {
        crf_spinbox_->setValue(std::get<int>(crf_it->second));
    }
    
    // Load bitrate
    auto bitrate_it = params.find("encoder_bitrate");
    if (bitrate_it != params.end() && std::holds_alternative<int>(bitrate_it->second)) {
        int bitrate = std::get<int>(bitrate_it->second);
        bitrate_spinbox_->setValue(bitrate / 1000000);  // Convert to Mbps
    }
    
    // Load options
    auto audio_it = params.find("embed_audio");
    if (audio_it != params.end() && std::holds_alternative<bool>(audio_it->second)) {
        embed_audio_checkbox_->setChecked(std::get<bool>(audio_it->second));
    }
    
    auto captions_it = params.find("embed_closed_captions");
    if (captions_it != params.end() && std::holds_alternative<bool>(captions_it->second)) {
        embed_captions_checkbox_->setChecked(std::get<bool>(captions_it->second));
    }
    
    updating_ui_ = false;
}

void FFmpegPresetDialog::on_category_changed(int index)
{
    if (updating_ui_) return;
    update_preset_list();
}

void FFmpegPresetDialog::on_preset_changed(int index)
{
    if (updating_ui_) return;
    update_preset_description();
    
    // Show/hide hardware encoder group based on preset
    if (index >= 0 && index < static_cast<int>(current_category_presets_.size())) {
        const auto& preset = current_category_presets_[index];
        hardware_group_->setVisible(preset.supports_hardware && !available_hw_encoders_.empty());
        deinterlace_checkbox_->setEnabled(preset.supports_deinterlace);
    }
}

void FFmpegPresetDialog::on_hardware_encoder_changed(int index)
{
    if (updating_ui_) return;
    
    if (index == 0) {
        hardware_status_label_->setText("Using software encoding (slower but compatible)");
    } else {
        if (!available_hw_encoders_.empty()) {
            hardware_status_label_->setText(QString("Using hardware encoder: %1 (faster)")
                .arg(QString::fromStdString(available_hw_encoders_[0])));
        }
    }
}

void FFmpegPresetDialog::on_deinterlace_changed(Qt::CheckState state)
{
    // Could update description or show warning
    Q_UNUSED(state);
}

void FFmpegPresetDialog::update_preset_list()
{
    updating_ui_ = true;
    
    current_category_presets_.clear();
    preset_combo_->clear();
    
    int category = category_combo_->currentIndex();
    
    // Filter presets by category
    for (const auto& preset : all_presets_) {
        bool include = false;
        
        switch (category) {
            case 0:  // Lossless/Archive
                include = (preset.codec == "ffv1" || preset.format_string.find("lossless") != std::string::npos);
                break;
            case 1:  // Professional/ProRes
                include = (preset.codec.find("prores") != std::string::npos);
                break;
            case 2:  // Uncompressed
                include = (preset.codec == "v210" || preset.codec == "v410");
                break;
            case 3:  // Broadcast
                include = (preset.codec == "mpeg2video");
                break;
            case 4:  // Universal (H.264)
                include = (preset.codec == "h264" && preset.format_string.find("_lossless") == std::string::npos);
                break;
            case 5:  // Modern (H.265/AV1)
                include = (preset.codec == "hevc" || preset.codec == "av1" || preset.codec == "av1_lossless");
                break;
            case 6:  // Hardware Accelerated
                include = preset.supports_hardware;
                break;
        }
        
        if (include) {
            current_category_presets_.push_back(preset);
            preset_combo_->addItem(QString::fromStdString(preset.name));
        }
    }
    
    updating_ui_ = false;
    
    if (preset_combo_->count() > 0) {
        preset_combo_->setCurrentIndex(0);
        update_preset_description();
    }
}

void FFmpegPresetDialog::update_preset_description()
{
    int index = preset_combo_->currentIndex();
    if (index >= 0 && index < static_cast<int>(current_category_presets_.size())) {
        const auto& preset = current_category_presets_[index];
        description_label_->setText(QString::fromStdString(preset.description));
    } else {
        description_label_->setText("No preset selected");
    }
}

void FFmpegPresetDialog::detect_available_hardware_encoders()
{
    // This is a simplified detection - in a real implementation, you'd
    // probe FFmpeg for available encoders
    
    // For now, just check common ones
    // TODO: Actually probe FFmpeg codecs using avcodec_find_encoder_by_name
    
    #ifdef __linux__
        // Check for VA-API
        available_hw_encoders_.push_back("vaapi");
    #endif
    
    #ifdef __APPLE__
        // VideoToolbox is usually available on macOS
        available_hw_encoders_.push_back("videotoolbox");
    #endif
    
    // Note: Real implementation should probe:
    // - NVENC (nvidia)
    // - QuickSync (qsv)
    // - AMF (amd)
    // by trying to create encoders
}
