/*
 * File:        comb.cpp
 * Module:      orc-core
 * Purpose:     NN NTSC comb filter decoder
 *
 * Core algorithm change: split3D() replaced with a 4-field overlap-add (OLA)
 * pipeline that runs each 16×16×4 spatial-temporal tile through a 3D FFT
 * (FFTW3), feeds the spectral magnitudes into an ONNX neural network
 * (chroma_net_v2.onnx), applies the per-bin gain mask, and IFFT's back.
 * The NN model and all runtime optimisations (thread-safe FFTW plan creation,
 * thread-local FFTW buffers, thread-safe ONNX session) are from
 * simoninns/nnTransform3D.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018 Chad Page
 * SPDX-FileCopyrightText: 2018-2019 Simon Inns
 * SPDX-FileCopyrightText: 2020-2021 Adam Sampson
 * SPDX-FileCopyrightText: 2021 Phillip Blucas
 * SPDX-FileCopyrightText: 2026 Simon Inns (Modified for decode-orc)
 */

#include "comb.h"

// Shared filter tables (from orc/core/stages/chroma_sink/decoders/)
#include "deemp.h"
#include "firfilter.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

// FFTW3 (double precision) for 3D FFT tiles
#include <fftw3.h>

// ONNX Runtime for neural-network chroma mask inference
#include <onnxruntime_cxx_api.h>

// CMake-generated C array embedding the chroma_net_v2.onnx model bytes.
// Generated at configure time by orc/core/CMakeLists.txt from the vendored
// binary at stages/nn_ntsc_chroma_sink/resources/chroma_net_v2.onnx.
#include "chroma_net_v2_onnx_data.h"

#include "logging.h"

namespace orc::nn_ntsc {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// 3D array index helper
// ---------------------------------------------------------------------------
#ifndef IDX3
#define IDX3(t, y, x, Nt, Ny, Nx) ((t) * (Ny) * (Nx) + (y) * (Nx) + (x))
#endif

// Indexes for the candidates considered in adaptive 3D mode
enum CandidateIndex : int32_t {
    CAND_LEFT,
    CAND_RIGHT,
    CAND_UP,
    CAND_DOWN,
    CAND_PREV_FIELD,
    CAND_NEXT_FIELD,
    CAND_PREV_FRAME,
    CAND_NEXT_FRAME,
    NUM_CANDIDATES
};

// Since we are at exactly 4fsc, the sine value at sample position i
// follows a simple 4-element repeating pattern.
static constexpr double sin4fsc_data[] = {1.0, 0.0, -1.0, 0.0};

constexpr double sin4fsc(const int32_t i) {
    return sin4fsc_data[i % 4];
}
constexpr double cos4fsc(const int32_t i) {
    return sin4fsc(i + 1);
}

// ============================================================================
// Public methods
// ============================================================================

Comb::Comb()
    : configurationSet(false)
{
}

int32_t Comb::Configuration::getLookBehind() const {
    return (dimensions == 3) ? 1 : 0;
}

int32_t Comb::Configuration::getLookAhead() const {
    return (dimensions == 3) ? 2 : 0;
}

const Comb::Configuration &Comb::getConfiguration() const {
    return configuration;
}

void Comb::updateConfiguration(const NnVideoParameters &_videoParameters,
                               const Comb::Configuration &_configuration)
{
    videoParameters = _videoParameters;
    configuration   = _configuration;

    if (videoParameters.fieldWidth > MAX_WIDTH)
        ORC_LOG_ERROR("Comb::updateConfiguration(): Field width {} exceeds MAX_WIDTH {}",
                      videoParameters.fieldWidth, MAX_WIDTH);
    if ((videoParameters.fieldHeight * 2 - 1) > MAX_HEIGHT)
        ORC_LOG_ERROR("Comb::updateConfiguration(): Frame height {} exceeds MAX_HEIGHT {}",
                      (videoParameters.fieldHeight * 2 - 1), MAX_HEIGHT);
    if (videoParameters.activeVideoStart < 16)
        ORC_LOG_ERROR("Comb::updateConfiguration(): activeVideoStart {} must be > 16",
                      videoParameters.activeVideoStart);
    if (std::fabs((videoParameters.sampleRate / videoParameters.fSC) - 4.0) > 1.0e-6)
        ORC_LOG_ERROR("Comb::updateConfiguration(): Data is not in 4fsc sample rate — "
                      "colour decoding will not work properly");

    configurationSet = true;
}

// ---------------------------------------------------------------------------
// [REVISED] decodeFrames: 4-Field Block / 2-Field Step (Overlap-Add)
// ---------------------------------------------------------------------------
void Comb::decodeFrames(const std::vector<NnSourceField> &inputFields,
                        int32_t startIndex, int32_t endIndex,
                        std::vector<ComponentFrame>      &componentFrames)
{
    assert(configurationSet);
    assert((static_cast<int32_t>(componentFrames.size()) * 2) == (endIndex - startIndex));

    // Frame-buffer cache: OLA requires writing into future frames, so we keep a
    // map of in-flight FrameBuffers keyed by frame index (relative to startIndex).
    std::map<int, std::shared_ptr<FrameBuffer>> bufferCache;

    // Helper: get an existing FrameBuffer or construct one populated with fields.
    auto getFrameBuffer = [&](int frameIdx) -> std::shared_ptr<FrameBuffer> {
        auto it = bufferCache.find(frameIdx);
        if (it != bufferCache.end())
            return it->second;

        auto buf = std::make_shared<FrameBuffer>(videoParameters, configuration);

        const int fieldIdx1 = startIndex + frameIdx * 2;
        const int fieldIdx2 = fieldIdx1 + 1;

        if (fieldIdx1 >= 0 && fieldIdx2 < static_cast<int>(inputFields.size())) {
            buf->loadFields(inputFields[fieldIdx1], inputFields[fieldIdx2]);
            buf->split1D();
            buf->split2D();
        }
        // Else: buffer stays black (boundary frame handling).

        bufferCache[frameIdx] = buf;
        return buf;
    };

    // Step by 2 fields (1 frame).  4-field block: [Current, Next].
    for (int32_t fieldIndex = startIndex; fieldIndex < endIndex; fieldIndex += 2) {
        const int currentFrameIdx = (fieldIndex - startIndex) / 2;

        auto bufCurr = getFrameBuffer(currentFrameIdx);
        auto bufNext = getFrameBuffer(currentFrameIdx + 1);

        if (configuration.dimensions == 3) {
            // Process 4-field block; OLA accumulates into bufCurr AND bufNext.
            bufCurr->split3D(*bufNext, currentFrameIdx);
        }

        // Output current frame.
        if (currentFrameIdx >= 0 &&
            currentFrameIdx < static_cast<int>(componentFrames.size())) {

            componentFrames[currentFrameIdx].init(videoParameters);
            bufCurr->setComponentFrame(componentFrames[currentFrameIdx]);

            if (configuration.dimensions == 3)
                bufCurr->finalizeOLA();

            if (configuration.phaseCompensation)
                bufCurr->splitIQlocked();
            else {
                bufCurr->splitIQ();
                bufCurr->adjustY();
            }

            bufCurr->filterIQ();
            bufCurr->doCNR();
            bufCurr->doYNR();
            bufCurr->transformIQ(configuration.chromaGain, configuration.chromaPhase);

            bufferCache.erase(currentFrameIdx);
        }
    }
}

// ============================================================================
// FrameBuffer — private implementation
// ============================================================================

Comb::FrameBuffer::FrameBuffer(const NnVideoParameters &videoParameters_,
                               const Configuration     &configuration_)
    : videoParameters(videoParameters_)
    , configuration(configuration_)
{
    frameHeight = (videoParameters.fieldHeight * 2) - 1;
    irescale    = (videoParameters.white16bIre - videoParameters.black16bIre) / 100.0;

    // Initialise OLA accumulators.
    const int safeWidth  = videoParameters.fieldWidth;
    const int safeHeight = videoParameters.fieldHeight * 2;
    accChroma.assign(safeHeight, std::vector<double>(safeWidth, 0.0));
    weightSum.assign(safeHeight, std::vector<double>(safeWidth, 0.0));

    // Pre-fill rawbuffer with black so boundary reads are always valid.
    rawbuffer.assign(videoParameters.fieldWidth * frameHeight, 0);
}

int32_t Comb::FrameBuffer::getFieldID(int32_t lineNumber) const
{
    const bool isFirstField = ((lineNumber % 2) == 0);
    return isFirstField ? firstFieldPhaseID : secondFieldPhaseID;
}

bool Comb::FrameBuffer::getLinePhase(int32_t lineNumber) const
{
    const int32_t fieldID = getFieldID(lineNumber);
    const bool isPositivePhaseOnEvenLines = (fieldID == 1) || (fieldID == 4);
    const int  fieldLine  = (lineNumber / 2);
    const bool isEvenLine = (fieldLine % 2) == 0;
    return isEvenLine ? isPositivePhaseOnEvenLines : !isPositivePhaseOnEvenLines;
}

void Comb::FrameBuffer::loadFields(const NnSourceField &firstField,
                                   const NnSourceField &secondField)
{
    rawbuffer.clear();
    int32_t fieldLine = 0;
    for (int32_t frameLine = 0; frameLine < frameHeight; frameLine += 2) {
        (void)frameLine;
        const auto *src1 =
            firstField.data.data() + fieldLine * videoParameters.fieldWidth;
        rawbuffer.insert(rawbuffer.end(), src1,
                         src1 + videoParameters.fieldWidth);

        const auto *src2 =
            secondField.data.data() + fieldLine * videoParameters.fieldWidth;
        rawbuffer.insert(rawbuffer.end(), src2,
                         src2 + videoParameters.fieldWidth);
        fieldLine++;
    }

    firstFieldPhaseID  = firstField.field_phase_id;
    secondFieldPhaseID = secondField.field_phase_id;

    // Zero clpbuffer.
    for (int buf = 0; buf < 3; buf++)
        for (int y = 0; y < MAX_HEIGHT; y++)
            for (int x = 0; x < MAX_WIDTH; x++)
                clpbuffer[buf].pixel[y][x] = 0.0;

    componentFrame = nullptr;
}

void Comb::FrameBuffer::split1D()
{
    for (int32_t lineNumber = videoParameters.firstActiveFrameLine;
         lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        const uint16_t *line =
            rawbuffer.data() + lineNumber * videoParameters.fieldWidth;
        for (int32_t h = videoParameters.activeVideoStart;
             h < videoParameters.activeVideoEnd; h++) {
            const double tc1 =
                (line[h] - ((line[h - 2] + line[h + 2]) / 2.0)) / 2.0;
            clpbuffer[0].pixel[lineNumber][h] = tc1;
        }
    }
}

void Comb::FrameBuffer::split2D()
{
    static constexpr double blackLine[MAX_WIDTH] = {0};

    for (int32_t lineNumber = videoParameters.firstActiveFrameLine;
         lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {

        const double *previousLine = blackLine;
        if (lineNumber - 2 >= videoParameters.firstActiveFrameLine)
            previousLine = clpbuffer[0].pixel[lineNumber - 2];

        const double *currentLine = clpbuffer[0].pixel[lineNumber];

        const double *nextLine = blackLine;
        if (lineNumber + 2 < videoParameters.lastActiveFrameLine)
            nextLine = clpbuffer[0].pixel[lineNumber + 2];

        for (int32_t h = videoParameters.activeVideoStart;
             h < videoParameters.activeVideoEnd; h++) {
            double kp = 0.0, kn = 0.0;

            kp  = std::fabs(std::fabs(currentLine[h]) - std::fabs(previousLine[h]));
            kp += std::fabs(std::fabs(currentLine[h - 1]) - std::fabs(previousLine[h - 1]));
            kp -= (std::fabs(currentLine[h]) + std::fabs(previousLine[h - 1])) * 0.10;
            kn  = std::fabs(std::fabs(currentLine[h]) - std::fabs(nextLine[h]));
            kn += std::fabs(std::fabs(currentLine[h - 1]) - std::fabs(nextLine[h - 1]));
            kn -= (std::fabs(currentLine[h]) + std::fabs(nextLine[h - 1])) * 0.10;

            const double kRange = 45.0 * irescale;
            kp = std::clamp(1.0 - (kp / kRange), 0.0, 1.0);
            kn = std::clamp(1.0 - (kn / kRange), 0.0, 1.0);

            double sc = 1.0;
            if ((kn > 0.0) || (kp > 0.0)) {
                if (kn > (3.0 * kp)) kp = 0.0;
                else if (kp > (3.0 * kn)) kn = 0.0;
                sc = 2.0 / (kn + kp);
                if (sc < 1.0) sc = 1.0;
            } else {
                if ((std::fabs(std::fabs(previousLine[h]) - std::fabs(nextLine[h])) -
                     std::fabs((nextLine[h] + previousLine[h]) * 0.2)) <= 0.0) {
                    kn = kp = 1.0;
                }
            }

            double tc1 = 0.0;
            tc1 += ((currentLine[h] - previousLine[h]) * kp * sc);
            tc1 += ((currentLine[h] - nextLine[h])     * kn * sc);
            tc1 /= 4.0;

            clpbuffer[1].pixel[lineNumber][h] = tc1;
        }
    }
}

// ---------------------------------------------------------------------------
// [4-Field Block] split3D with ONNX NN + 50%-overlap sine-windowed OLA
// ---------------------------------------------------------------------------
void Comb::FrameBuffer::split3D(FrameBuffer &nextFrame, int frameIdx)
{
    // Tile dimensions
    static constexpr int Nx  = 16;
    static constexpr int Ny  = 16;
    static constexpr int Nt  = 4;

    // 50 % overlap step (required for perfect OLA reconstruction with sine window)
    static constexpr int STEP_X = 8;
    static constexpr int STEP_Y = 8;
    static constexpr int SC_X   = 4;  // subcarrier column stride matching

    // -------------------------------------------------------------------------
    // FFTW plans — created exactly once across all threads via std::call_once.
    // fftw_plan_dft_3d is NOT thread-safe; fftw_execute_dft IS thread-safe
    // provided each thread supplies its own in/out buffers (see thread_local below).
    // -------------------------------------------------------------------------
    static fftw_plan s_plan_fwd = nullptr;
    static fftw_plan s_plan_inv = nullptr;
    static std::once_flag s_fftw_once;
    std::call_once(s_fftw_once, []() {
        fftw_complex *tmp_in  = reinterpret_cast<fftw_complex *>(
            fftw_malloc(sizeof(fftw_complex) * Nt * Ny * Nx));
        fftw_complex *tmp_out = reinterpret_cast<fftw_complex *>(
            fftw_malloc(sizeof(fftw_complex) * Nt * Ny * Nx));
        s_plan_fwd = fftw_plan_dft_3d(Nt, Ny, Nx, tmp_in, tmp_out,
                                       FFTW_FORWARD,  FFTW_ESTIMATE);
        s_plan_inv = fftw_plan_dft_3d(Nt, Ny, Nx, tmp_out, tmp_in,
                                       FFTW_BACKWARD, FFTW_ESTIMATE);
        fftw_free(tmp_in);
        fftw_free(tmp_out);
    });

    // Thread-local FFTW working buffers: allocated once per thread, reused
    // for every tile — eliminates per-tile heap allocation (~12 000 allocs/frame).
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static thread_local fftw_complex tl_in[1024];
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static thread_local fftw_complex tl_out[1024];
    fftw_complex *in  = tl_in;
    fftw_complex *out = tl_out;

    // Sine OLA windows — computed once from fixed Nx/Ny/Nt dimensions.
    static double s_winX[16], s_winY[16], s_winT[4];
    static std::once_flag s_win_once;
    std::call_once(s_win_once, []() {
        for (int i = 0; i < 16; ++i) {
            s_winX[i] = std::sin(M_PI * (i + 0.5) / 16.0);
            s_winY[i] = s_winX[i];
        }
        for (int i = 0; i < 4; ++i)
            s_winT[i] = std::sin(M_PI * (i + 0.5) / 4.0);
    });
    const double *winX = s_winX;
    const double *winY = s_winY;
    const double *winT = s_winT;

    // -------------------------------------------------------------------------
    // ONNX Runtime — session loaded exactly once (thread-safe via call_once).
    // Session::Run is thread-safe for concurrent calls on one shared session.
    // -------------------------------------------------------------------------
    // Thread-local ONNX working memory — avoids heap allocation per tile.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static thread_local float          tl_tensor[2048];  // [2, 4, 16, 16]
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static thread_local float          tl_mags[1024];    // [4, 16, 16] magnitudes
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static thread_local Ort::MemoryInfo tl_mem_info =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    static std::unique_ptr<Ort::Env>     s_env;
    static std::unique_ptr<Ort::Session> s_session;
    static bool                          s_model_loaded = false;
    static std::once_flag                s_model_once;

    std::call_once(s_model_once, []() {
        try {
            s_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING,
                                               "NnNtscChromaDecoder");

            // The model bytes are compiled directly into the binary as a C
            // array by CMake (chroma_net_v2_onnx_data.cpp).  No file I/O needed.
            if (kChromaNetV2OnnxDataSize == 0) {
                ORC_LOG_ERROR("NnComb: Embedded ONNX model data is empty");
                return;
            }

            Ort::SessionOptions opts;
            opts.SetIntraOpNumThreads(1);   // NN is single-threaded per call
            opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

            s_session = std::make_unique<Ort::Session>(
                *s_env,
                static_cast<const void *>(kChromaNetV2OnnxData),
                kChromaNetV2OnnxDataSize,
                opts);

            s_model_loaded = true;
            ORC_LOG_INFO("NnComb: ONNX model loaded successfully ({} bytes)",
                         kChromaNetV2OnnxDataSize);
        } catch (const std::exception &e) {
            ORC_LOG_ERROR("NnComb: Failed to load ONNX model: {}", e.what());
        }
    });

    // -------------------------------------------------------------------------
    // Tile iteration: extend past image edges so window centres can reach them.
    // -------------------------------------------------------------------------
    FrameBuffer *frames[2] = {this, &nextFrame};

    const int startY = videoParameters.firstActiveFrameLine - (Ny / 2);
    const int endY   = videoParameters.lastActiveFrameLine;
    const int startX = videoParameters.activeVideoStart - (Nx / 2);
    const int endX   = videoParameters.activeVideoEnd;

    for (int y = startY; y < endY; y += STEP_Y) {
        for (int x = startX; x < endX; x += STEP_X) {

            // -----------------------------------------------------------------
            // A. Fill input tile (with zero-padding outside active area)
            // -----------------------------------------------------------------
            memset(in, 0, sizeof(fftw_complex) * Nt * Ny * Nx);

            double blockDC   = 0.0;
            int    pixelCount = 0;

            for (int f = 0; f < 2; ++f) {
                for (int sub_t = 0; sub_t < 2; ++sub_t) {
                    const int  t          = f * 2 + sub_t;
                    const bool isOddField = (t % 2 != 0);

                    for (int dy = 0; dy < Ny; ++dy) {
                        const int  absY    = y + dy;
                        const bool isYIn  = (absY >= videoParameters.firstActiveFrameLine &&
                                              absY <  videoParameters.lastActiveFrameLine);
                        const bool isOddLine = (absY % 2 != 0);

                        // Skip lines whose polarity doesn't match this sub-field.
                        if (isOddLine != isOddField) continue;

                        if (isYIn) {
                            const uint16_t *lineData =
                                frames[f]->rawbuffer.data() +
                                absY * videoParameters.fieldWidth;

                            for (int dx = 0; dx < Nx; ++dx) {
                                const int  absX  = x + dx;
                                const bool isXIn = (absX >= videoParameters.activeVideoStart &&
                                                    absX <  videoParameters.activeVideoEnd);
                                if (isXIn) {
                                    const int    idx = IDX3(t, dy, dx, Nt, Ny, Nx);
                                    const double val = static_cast<double>(lineData[absX]);
                                    in[idx][0] = val;
                                    blockDC   += val;
                                    pixelCount++;
                                }
                            }
                        }
                    }
                }
            }

            // Skip tiles that lie entirely outside the active picture.
            if (pixelCount == 0) continue;

            blockDC /= static_cast<double>(pixelCount);

            // -----------------------------------------------------------------
            // DC removal + sine windowing
            // -----------------------------------------------------------------
            for (int t = 0; t < Nt; ++t) {
                const bool isOddField = (t % 2 != 0);
                for (int dy = 0; dy < Ny; ++dy) {
                    const int  absY     = y + dy;
                    const bool isYIn   = (absY >= videoParameters.firstActiveFrameLine &&
                                          absY <  videoParameters.lastActiveFrameLine);
                    const bool isOddLine = (absY % 2 != 0);

                    for (int dx = 0; dx < Nx; ++dx) {
                        const int  absX  = x + dx;
                        const bool isXIn = (absX >= videoParameters.activeVideoStart &&
                                            absX <  videoParameters.activeVideoEnd);
                        const int  idx   = IDX3(t, dy, dx, Nt, Ny, Nx);

                        if (isYIn && isXIn && (isOddLine == isOddField)) {
                            in[idx][0] =
                                (in[idx][0] - blockDC) * winT[t] * winY[dy] * winX[dx];
                        } else {
                            in[idx][0] = 0.0;
                        }
                    }
                }
            }

            // -----------------------------------------------------------------
            // B. Forward FFT
            // -----------------------------------------------------------------
            fftw_execute_dft(s_plan_fwd, in, out);

            // -----------------------------------------------------------------
            // C. NN inference: apply per-bin gain mask
            // -----------------------------------------------------------------
            if (s_model_loaded) {
                // Precompute all 1024 magnitudes once (float precision).
                for (int i = 0; i < Nt * Ny * Nx; ++i) {
                    tl_mags[i] = std::sqrtf(
                        static_cast<float>(out[i][0] * out[i][0] +
                                           out[i][1] * out[i][1]));
                }

                // Channel 0: original magnitude — sequential copy.
                memcpy(tl_tensor, tl_mags, 1024 * sizeof(float));

                // Channel 1: reflected magnitude via Hermitian symmetry.
                int ptr = 1024;
                for (int ft = 0; ft < Nt; ++ft) {
                    const int ref_t = ((2 - ft) % 4 + 4) % 4;
                    for (int fy = 0; fy < Ny; ++fy) {
                        const int ref_y = (16 - fy) % 16;
                        for (int fx = 0; fx < Nx; ++fx) {
                            const int ref_x = (8 - fx + 16) % 16;
                            tl_tensor[ptr++] =
                                tl_mags[IDX3(ref_t, ref_y, ref_x, Nt, Ny, Nx)];
                        }
                    }
                }

                // Create input tensor wrapping tl_tensor in-place (no copy or alloc).
                static const int64_t input_shape[] = {1, 2, 4, 16, 16};
                Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
                    tl_mem_info, tl_tensor, 2048, input_shape, 5);

                // Run inference.
                static const char *input_names[]  = {"input"};
                static const char *output_names[] = {"output"};
                auto output_tensors = s_session->Run(
                    Ort::RunOptions{nullptr},
                    input_names,  &input_tensor, 1,
                    output_names, 1);

                // Apply gain mask — flat loop (avoids triple-nested IDX3 overhead).
                const float *mask_data =
                    output_tensors[0].GetTensorMutableData<float>();
                for (int i = 0; i < Nt * Ny * Nx; ++i) {
                    out[i][0] *= mask_data[i];
                    out[i][1] *= mask_data[i];
                }
            }

            // -----------------------------------------------------------------
            // D. Inverse FFT
            // -----------------------------------------------------------------
            fftw_execute_dft(s_plan_inv, out, in);

            // -----------------------------------------------------------------
            // E. Overlap-add accumulation (with boundary guards)
            // -----------------------------------------------------------------
            for (int t = 0; t < Nt; ++t) {
                const int  f_idx      = t / 2;
                const bool isOddField = (t % 2 != 0);
                FrameBuffer *targetFrame = frames[f_idx];

                for (int dy = 0; dy < Ny; ++dy) {
                    const int absY = y + dy;
                    if (absY < videoParameters.firstActiveFrameLine ||
                        absY >= videoParameters.lastActiveFrameLine)
                        continue;
                    if ((absY % 2) != static_cast<int>(isOddField)) continue;

                    for (int dx = 0; dx < Nx; ++dx) {
                        const int absX = x + dx;
                        if (absX < videoParameters.activeVideoStart ||
                            absX >= videoParameters.activeVideoEnd)
                            continue;

                        const int    idx = IDX3(t, dy, dx, Nt, Ny, Nx);
                        const double val =
                            in[idx][0] / static_cast<double>(Nt * Ny * Nx);
                        const double w = winT[t] * winY[dy] * winX[dx];

                        targetFrame->accChroma[absY][absX] += val * w;
                        targetFrame->weightSum[absY][absX] += w * w;
                    }
                }
            }
        }
    }
    // tl_in / tl_out are thread_local statics — nothing to free.
}

// ---------------------------------------------------------------------------
// finalizeOLA: normalise OLA accumulator into clpbuffer[2]
// ---------------------------------------------------------------------------
void Comb::FrameBuffer::finalizeOLA()
{
    for (int y = videoParameters.firstActiveFrameLine;
         y < videoParameters.lastActiveFrameLine; ++y) {
        for (int x = videoParameters.activeVideoStart;
             x < videoParameters.activeVideoEnd; ++x) {
            const double w = weightSum[y][x];
            clpbuffer[2].pixel[y][x] = (w > 1e-5)
                ? (accChroma[y][x] / w)
                : 0.0;  // Outside all window centres: black chroma
        }
    }
}

// ---------------------------------------------------------------------------
// IQ demodulation helpers
// ---------------------------------------------------------------------------
namespace {

struct BurstInfo { double bsin, bcos; };

constexpr double ROTATE_SIN =  0.5446390350150271;
constexpr double ROTATE_COS =  0.838670567945424;

BurstInfo detectBurst(const uint16_t *lineData,
                      const NnVideoParameters &vp)
{
    double bsin = 0.0, bcos = 0.0;
    for (int32_t i = vp.colourBurstStart; i < vp.colourBurstEnd; i++) {
        bsin += lineData[i] * sin4fsc(i);
        bcos += lineData[i] * cos4fsc(i);
    }
    const int32_t colourBurstLength = vp.colourBurstEnd - vp.colourBurstStart;
    bsin /= colourBurstLength;
    bcos /= colourBurstLength;
    const double burstNorm =
        std::max(std::sqrt(bsin * bsin + bcos * bcos), 130000.0 / 128.0);
    bsin /= burstNorm;
    bcos /= burstNorm;
    return {bsin, bcos};
}

} // namespace

void Comb::FrameBuffer::splitIQlocked()
{
    for (int32_t lineNumber = videoParameters.firstActiveFrameLine;
         lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        const uint16_t *line =
            rawbuffer.data() + lineNumber * videoParameters.fieldWidth;
        const auto info = detectBurst(line, videoParameters);
        double *Y = componentFrame->y(lineNumber);
        double *I = componentFrame->u(lineNumber);
        double *Q = componentFrame->v(lineNumber);

        for (int32_t h = videoParameters.activeVideoStart;
             h < videoParameters.activeVideoEnd; h++) {
            const auto val  = clpbuffer[configuration.dimensions - 1].pixel[lineNumber][h];
            const auto lsin = val * sin4fsc(h) * 2.0;
            const auto lcos = val * cos4fsc(h) * 2.0;
            const auto ti   = (lsin * info.bcos - lcos * info.bsin);
            const auto tq   = (lsin * info.bsin + lcos * info.bcos);
            I[h + 1] = ti * ROTATE_COS - tq * -ROTATE_SIN;
            Q[h + 1] = -(ti * -ROTATE_SIN + tq * ROTATE_COS);
            Y[h]     = line[h] - val;
        }
    }
}

void Comb::FrameBuffer::splitIQ()
{
    for (int32_t lineNumber = videoParameters.firstActiveFrameLine;
         lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        const uint16_t *line =
            rawbuffer.data() + lineNumber * videoParameters.fieldWidth;
        double *Y = componentFrame->y(lineNumber);
        double *I = componentFrame->u(lineNumber);
        double *Q = componentFrame->v(lineNumber);
        const bool linePhase = getLinePhase(lineNumber);
        double si = 0.0, sq = 0.0;

        for (int32_t h = videoParameters.activeVideoStart;
             h < videoParameters.activeVideoEnd; h++) {
            const int32_t phase = h % 4;
            double cavg = clpbuffer[configuration.dimensions - 1].pixel[lineNumber][h];
            if (linePhase) cavg = -cavg;
            switch (phase) {
            case 0: sq =  cavg; break;
            case 1: si = -cavg; break;
            case 2: sq = -cavg; break;
            case 3: si =  cavg; break;
            }
            Y[h] = line[h];
            I[h] = si;
            Q[h] = sq;
        }
    }
}

void Comb::FrameBuffer::filterIQ()
{
    auto iqFilter = makeFIRFilter(c_colorlp_b);
    const int width = videoParameters.activeVideoEnd - videoParameters.activeVideoStart;
    std::vector<double> tempBuf(width);

    for (int32_t lineNumber = videoParameters.firstActiveFrameLine;
         lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        double *I = componentFrame->u(lineNumber) + videoParameters.activeVideoStart;
        double *Q = componentFrame->v(lineNumber) + videoParameters.activeVideoStart;
        iqFilter.apply(I, tempBuf.data(), width);
        std::copy(tempBuf.begin(), tempBuf.end(), I);
        iqFilter.apply(Q, tempBuf.data(), width);
        std::copy(tempBuf.begin(), tempBuf.end(), Q);
    }
}

void Comb::FrameBuffer::adjustY()
{
    for (int32_t lineNumber = videoParameters.firstActiveFrameLine;
         lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        double *Y = componentFrame->y(lineNumber);
        double *I = componentFrame->u(lineNumber);
        double *Q = componentFrame->v(lineNumber);
        const bool linePhase = getLinePhase(lineNumber);

        for (int32_t h = videoParameters.activeVideoStart;
             h < videoParameters.activeVideoEnd; h++) {
            double comp = 0.0;
            const int32_t phase = h % 4;
            switch (phase) {
            case 0: comp = -Q[h]; break;
            case 1: comp =  I[h]; break;
            case 2: comp =  Q[h]; break;
            case 3: comp = -I[h]; break;
            }
            if (!linePhase) comp = -comp;
            Y[h] -= comp;
        }
    }
}

void Comb::FrameBuffer::doCNR()
{
    if (configuration.cNRLevel == 0.0) return;
    const double nr_c = configuration.cNRLevel * irescale;
    auto iFilter(f_nrc);
    auto qFilter(f_nrc);
    const int32_t delay = static_cast<int32_t>(c_nrc_b.size()) / 2;
    std::vector<double> hpI(videoParameters.activeVideoEnd + delay);
    std::vector<double> hpQ(videoParameters.activeVideoEnd + delay);

    for (int32_t lineNumber = videoParameters.firstActiveFrameLine;
         lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        double *I = componentFrame->u(lineNumber);
        double *Q = componentFrame->v(lineNumber);

        for (int32_t h = videoParameters.activeVideoStart - delay;
             h < videoParameters.activeVideoStart; h++) {
            iFilter.feed(0.0);
            qFilter.feed(0.0);
        }
        for (int32_t h = videoParameters.activeVideoStart;
             h < videoParameters.activeVideoEnd; h++) {
            hpI[h] = iFilter.feed(I[h]);
            hpQ[h] = qFilter.feed(Q[h]);
        }
        for (int32_t h = videoParameters.activeVideoEnd;
             h < videoParameters.activeVideoEnd + delay; h++) {
            hpI[h] = iFilter.feed(0.0);
            hpQ[h] = qFilter.feed(0.0);
        }
        for (int32_t h = videoParameters.activeVideoStart;
             h < videoParameters.activeVideoEnd; h++) {
            double ai = hpI[h + delay];
            double aq = hpQ[h + delay];
            if (std::fabs(ai) > nr_c) ai = (ai > 0.0) ?  nr_c : -nr_c;
            if (std::fabs(aq) > nr_c) aq = (aq > 0.0) ?  nr_c : -nr_c;
            I[h] -= ai;
            Q[h] -= aq;
        }
    }
}

void Comb::FrameBuffer::doYNR()
{
    if (configuration.yNRLevel == 0.0) return;
    const double nr_y = configuration.yNRLevel * irescale;
    auto yFilter(f_nr);
    const int32_t delay = static_cast<int32_t>(c_nr_b.size()) / 2;
    std::vector<double> hpY(videoParameters.activeVideoEnd + delay);

    for (int32_t lineNumber = videoParameters.firstActiveFrameLine;
         lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        double *Y = componentFrame->y(lineNumber);

        for (int32_t h = videoParameters.activeVideoStart - delay;
             h < videoParameters.activeVideoStart; h++)
            yFilter.feed(0.0);
        for (int32_t h = videoParameters.activeVideoStart;
             h < videoParameters.activeVideoEnd; h++)
            hpY[h] = yFilter.feed(Y[h]);
        for (int32_t h = videoParameters.activeVideoEnd;
             h < videoParameters.activeVideoEnd + delay; h++)
            hpY[h] = yFilter.feed(0.0);
        for (int32_t h = videoParameters.activeVideoStart;
             h < videoParameters.activeVideoEnd; h++) {
            double a = hpY[h + delay];
            if (std::fabs(a) > nr_y) a = (a > 0.0) ? nr_y : -nr_y;
            Y[h] -= a;
        }
    }
}

void Comb::FrameBuffer::transformIQ(double chromaGain, double chromaPhase)
{
    const double theta = ((33.0 + chromaPhase) * M_PI) / 180.0;
    const double bp    = std::sin(theta) * chromaGain;
    const double bq    = std::cos(theta) * chromaGain;

    for (int32_t lineNumber = videoParameters.firstActiveFrameLine;
         lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        double *I = componentFrame->u(lineNumber);
        double *Q = componentFrame->v(lineNumber);
        for (int32_t h = videoParameters.activeVideoStart;
             h < videoParameters.activeVideoEnd; h++) {
            const double U = (-bp * I[h]) + (bq * Q[h]);
            const double V = ( bq * I[h]) + (bp * Q[h]);
            I[h] = U;
            Q[h] = V;
        }
    }
}

void Comb::FrameBuffer::overlayMap(const FrameBuffer & /*previousFrame*/,
                                   const FrameBuffer & /*nextFrame*/)
{
    // Debug overlay not needed in decode-orc; intentionally empty.
}

// ---------------------------------------------------------------------------
// Adaptive-candidate helpers (used by 2D decoder path and for map overlay)
// ---------------------------------------------------------------------------
void Comb::FrameBuffer::getBestCandidate(int32_t lineNumber, int32_t h,
                                          const FrameBuffer &previousFrame,
                                          const FrameBuffer &nextFrame,
                                          int32_t &bestIndex,
                                          double  &bestSample) const
{
    Candidate candidates[NUM_CANDIDATES];
    static constexpr double LINE_BONUS  = -2.0;
    static constexpr double FIELD_BONUS = LINE_BONUS  - 2.0;
    static constexpr double FRAME_BONUS = FIELD_BONUS - 2.0;

    candidates[CAND_LEFT]  = getCandidate(lineNumber, h, *this, lineNumber, h - 2, 0.0);
    candidates[CAND_RIGHT] = getCandidate(lineNumber, h, *this, lineNumber, h + 2, 0.0);
    candidates[CAND_UP]    = getCandidate(lineNumber, h, *this, lineNumber - 2, h, LINE_BONUS);
    candidates[CAND_DOWN]  = getCandidate(lineNumber, h, *this, lineNumber + 2, h, LINE_BONUS);

    if (getLinePhase(lineNumber) == getLinePhase(lineNumber - 1)) {
        candidates[CAND_PREV_FIELD] =
            getCandidate(lineNumber, h, previousFrame, lineNumber - 1, h, FIELD_BONUS);
        candidates[CAND_NEXT_FIELD] =
            getCandidate(lineNumber, h, *this, lineNumber + 1, h, FIELD_BONUS);
    } else {
        candidates[CAND_PREV_FIELD] =
            getCandidate(lineNumber, h, *this, lineNumber - 1, h, FIELD_BONUS);
        candidates[CAND_NEXT_FIELD] =
            getCandidate(lineNumber, h, nextFrame, lineNumber + 1, h, FIELD_BONUS);
    }
    candidates[CAND_PREV_FRAME] =
        getCandidate(lineNumber, h, previousFrame, lineNumber, h, FRAME_BONUS);
    candidates[CAND_NEXT_FRAME] =
        getCandidate(lineNumber, h, nextFrame, lineNumber, h, FRAME_BONUS);

    if (configuration.adaptive) {
        bestIndex = 0;
        for (int32_t i = 1; i < NUM_CANDIDATES; i++)
            if (candidates[i].penalty < candidates[bestIndex].penalty)
                bestIndex = i;
    } else {
        bestIndex = CAND_PREV_FRAME;
    }
    bestSample = candidates[bestIndex].sample;
}

Comb::FrameBuffer::Candidate
Comb::FrameBuffer::getCandidate(int32_t refLineNumber, int32_t refH,
                                const FrameBuffer &frameBuffer,
                                int32_t lineNumber, int32_t h,
                                double adjustPenalty) const
{
    Candidate result;
    result.sample = frameBuffer.clpbuffer[0].pixel[lineNumber][h];

    if (lineNumber < videoParameters.firstActiveFrameLine ||
        lineNumber >= videoParameters.lastActiveFrameLine) {
        result.penalty = 1000.0;
        return result;
    }

    const int32_t wantPhase =
        (2 + (getLinePhase(refLineNumber) ? 2 : 0) + refH) % 4;
    const int32_t havePhase =
        ((frameBuffer.getLinePhase(lineNumber) ? 2 : 0) + h) % 4;
    if (wantPhase != havePhase) {
        result.penalty = 1000.0;
        return result;
    }

    const uint16_t *refLine =
        rawbuffer.data() + refLineNumber * videoParameters.fieldWidth;
    const uint16_t *candidateLine =
        frameBuffer.rawbuffer.data() + lineNumber * videoParameters.fieldWidth;

    double yPenalty = 0.0;
    for (int32_t offset = -1; offset < 2; offset++) {
        const double refC      = clpbuffer[1].pixel[refLineNumber][refH + offset];
        const double refY      = refLine[refH + offset] - refC;
        const double candidateC =
            frameBuffer.clpbuffer[1].pixel[lineNumber][h + offset];
        const double candidateY = candidateLine[h + offset] - candidateC;
        yPenalty += std::fabs(refY - candidateY);
    }
    yPenalty = yPenalty / 3.0 / irescale;

    double iqPenalty = 0.0;
    static constexpr double weights[] = {0.5, 1.0, 0.5};
    for (int32_t offset = -1; offset < 2; offset++) {
        const double refC      = clpbuffer[1].pixel[refLineNumber][refH + offset];
        const double candidateC =
            -frameBuffer.clpbuffer[1].pixel[lineNumber][h + offset];
        iqPenalty += std::fabs(refC - candidateC) * weights[offset + 1];
    }
    iqPenalty = (iqPenalty / 2.0 / irescale) * 0.28;

    result.penalty = yPenalty + iqPenalty + adjustPenalty;
    return result;
}

}  // namespace orc::nn_ntsc
