/*
 * File:        ld_sink_stage_deps.h
 * Module:      orc-core
 * Purpose:     ld-decode Sink Stage dependencies
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_LD_SINK_STAGE_DEPS_H
#define ORC_CORE_LD_SINK_STAGE_DEPS_H

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

#include "ld_sink_stage_deps_interface.h"
#include "tbc_metadata_writer_interface.h"
#include "observation_context_interface.h"
#include "video_field_representation.h"
#include "../triggerable_stage.h"
#include "../factories_interface.h"

namespace orc
{
    class LDSinkStageDeps : public ILDSinkStageDeps
    {
    public:
        LDSinkStageDeps(IFactories& factories, std::shared_ptr<ITBCMetadataWriter> metadata_writer)
            : factories_(factories), metadata_writer_(std::move(metadata_writer)) {}

        /**
         * @brief Sets dependencies that aren't interfaces.
         *
         * @param progress_callback The progress callback
         * @param pIsProcessing Pointer to is_processing atomic bool
         * @param pCancelRequested Pointer to cancel_requested atomic bool
         */
        void init(TriggerProgressCallback progress_callback, std::atomic<bool>* pIsProcessing, std::atomic<bool>* pCancelRequested);

        bool write_tbc_and_metadata(
            const VideoFieldRepresentation* representation,
            const std::string& tbc_path,
            IObservationContext& observation_context) override;

    private:
        TriggerProgressCallback progress_callback_;
        std::atomic<bool>* pIsProcessing_{};
        std::atomic<bool>* pCancelRequested_{};
        IFactories& factories_;
        std::shared_ptr<ITBCMetadataWriter> metadata_writer_;
    };
} // namespace orc

#endif // ORC_CORE_LD_SINK_STAGE_DEPS_H
