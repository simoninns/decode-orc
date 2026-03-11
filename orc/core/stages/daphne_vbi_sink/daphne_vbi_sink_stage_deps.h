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

        // need IFactories to create file writer.
        // Intentionally chose shared_ptr for daphne_vbi_writer_util instead of unique_ptr to decrease chances of subtle memory de-allocation defects
        // 'daphne_vbi_writer_util' needs to be shared_ptr (as opposed to normal pointer) because the parent class won't necessarily keep the onject instantiated,
        //      so this class may become the sole owner.
        DaphneVBISinkStageDeps(IFactories *pFactories, std::shared_ptr<IDaphneVBIWriterUtil> daphne_vbi_writer_util) : pFactories_(pFactories),
            daphne_vbi_writer_util_(std::move(daphne_vbi_writer_util)) {}

        void init(TriggerProgressCallback progress_callback, std::atomic<bool> *pIsProcessing, std::atomic<bool> *pCancelRequested);

        bool write_vbi(
            const VideoFieldRepresentation* representation,
            const std::string& vbi_path,
            IObservationContext *pObservationContext) override;

    private:
        TriggerProgressCallback progress_callback_;  // Progress callback for trigger operations
        std::atomic<bool> *pIsProcessing_{};
        std::atomic<bool> *pCancelRequested_{};
        IFactories *pFactories_;
        std::shared_ptr<IDaphneVBIWriterUtil> daphne_vbi_writer_util_;
    };
}

#endif //DECODE_ORC_ROOT_DAPHNE_VBI_SINK_STAGE_DEPS_H