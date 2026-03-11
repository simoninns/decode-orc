/*
* File:        stage_factories.h
 * Module:      orc-core
 * Purpose:     Implement abstract factory design pattern ( https://en.wikipedia.org/wiki/Abstract_factory_pattern ) to
 *                  a) encourage encapsulation in the architecture and
 *                  b) make mocking in unit tests easier
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#ifndef DECODE_ORC_ROOT_STAGE_FACTORIES_H
#define DECODE_ORC_ROOT_STAGE_FACTORIES_H

#include "stage_factories_interface.h"
#include "../factories_interface.h"

namespace orc
{
    class StageFactories : public IStageFactories
    {
    public:

        StageFactories(IFactories *pFactories) : pFactories_(pFactories) {}

        std::shared_ptr<IDaphneVBISinkStageDeps> CreateInstanceDaphneVBISinkStageDeps(TriggerProgressCallback progress_callback, std::atomic<bool> *pIsProcessing, std::atomic<bool> *pCancelRequested) override;

    private:

        // At the moment, this object is special in that we assume the parent is managing the memory for this instance.
        // Perhaps we should change this to be shared_ptr to reduce 'tribal knowledge.'
        IFactories* pFactories_;
    };
} // orc

#endif //DECODE_ORC_ROOT_STAGE_FACTORIES_H