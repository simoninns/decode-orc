/*
 * File:        daphne_vbi_sink_stage_deps.h
 * Module:      orc-core
 * Purpose:     Generate .VBI binary files, dependencies
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#ifndef DECODE_ORC_ROOT_DAPHNE_VBI_SINK_STAGE_DEPS_H
#define DECODE_ORC_ROOT_DAPHNE_VBI_SINK_STAGE_DEPS_H

#include <atomic>
#include <utility>

#include "daphne_vbi_sink_stage_deps_interface.h"
#include "daphne_vbi_writer_util_interface.h"
#include "observation_context_interface.h"
#include "video_field_representation.h"
#include "../triggerable_stage.h"
#include "../factories_interface.h"

namespace orc
{
    class DaphneVBISinkStageDeps : public IDaphneVBISinkStageDeps
    {
    public:

        // This class needs IFactories to create file writer and IStageFactories to create DaphneVBIWriterUtil
        DaphneVBISinkStageDeps(IFactories &factories, IStageFactories &stageFactories) : factories_(factories), stageFactories_(stageFactories) {}

        /**
         * @brief Sets dependencies that aren't interfaces.
         *
         * @param progress_callback The progress callback
         * @param pIsProcessing Pointer to is_processing bool
         * @param pCancelRequested Pointer to cancel_requested bool
         */
        void init(TriggerProgressCallback progress_callback, std::atomic<bool> *pIsProcessing, std::atomic<bool> *pCancelRequested);

        bool write_vbi(
            const VideoFieldRepresentation* representation,
            const std::string& vbi_path,
            IObservationContext &observation_context) override;

    private:
        TriggerProgressCallback progress_callback_;  // Progress callback for trigger operations
        std::atomic<bool> *pIsProcessing_{};
        std::atomic<bool> *pCancelRequested_{};
        IFactories &factories_;
        IStageFactories &stageFactories_;
    };
}

#endif //DECODE_ORC_ROOT_DAPHNE_VBI_SINK_STAGE_DEPS_H