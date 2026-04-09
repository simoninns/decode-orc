/*
 * File:        nn_output_backend.h
 * Module:      orc-core
 * Purpose:     Abstract output backend for the NN NTSC chroma sink
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_NN_OUTPUT_BACKEND_H
#define ORC_CORE_NN_OUTPUT_BACKEND_H

#include "decoders/componentframe.h"
#include "decoders/nn_output_writer.h"
#include <fstream>
#include <memory>
#include <string>

namespace orc {

/**
 * @brief Abstract output backend for the NN NTSC chroma sink stage.
 *
 * Mirrors the OutputBackend interface used by ChromaSinkStage but takes
 * orc::nn_ntsc::ComponentFrame (the NN decoder's frame type) instead of the
 * global-namespace ::ComponentFrame used by the legacy ld-decode decoders.
 *
 * initialize() receives a mutable pointer to the NnVideoParameters so that
 * the backend can call NnOutputWriter::updateConfiguration(), which pads
 * width/height to the configured paddingAmount.  The caller must use those
 * padded values when configuring the per-thread NtscDecoder workers.
 */
class NnOutputBackend {
public:
    /**
     * @brief Configuration for the output backend.
     *
     * video_params is modified in place by initialize() to apply the padding
     * required by the writer.  The padded values must be used when configuring
     * the NtscDecoder workers.
     */
    struct Configuration {
        std::string output_path;
        std::string format;           ///< "rgb", "yuv", or "y4m"
        int32_t     padding_amount = 8;
        nn_ntsc::NnVideoParameters* video_params = nullptr;  ///< in/out: padded by initialize()
    };

    virtual ~NnOutputBackend() = default;

    virtual bool        initialize(Configuration& config) = 0;
    virtual bool        writeFrame(const nn_ntsc::ComponentFrame& frame) = 0;
    virtual bool        finalize() = 0;
    virtual std::string getFormatInfo() const = 0;
};

/**
 * @brief Raw-file output backend (RGB48, YUV444P16, Y4M).
 *
 * Wraps NnOutputWriter and a std::ofstream, handling stream/frame headers and
 * pixel-format conversion internally so trigger() only calls writeFrame().
 */
class NnRawOutputBackend : public NnOutputBackend {
public:
    ~NnRawOutputBackend() override;

    bool        initialize(Configuration& config) override;
    bool        writeFrame(const nn_ntsc::ComponentFrame& frame) override;
    bool        finalize() override;
    std::string getFormatInfo() const override;

private:
    nn_ntsc::NnOutputWriter writer_;
    std::ofstream           out_file_;
    std::string             frame_header_;  ///< cached per-frame Y4M header (empty for raw)
    std::string             format_info_;   ///< human-readable format name for logging
};

} // namespace orc

#endif // ORC_CORE_NN_OUTPUT_BACKEND_H
