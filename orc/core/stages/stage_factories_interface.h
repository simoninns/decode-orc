/*
 * File:        stage_factories_interface.h
 * Module:      orc-core
 * Purpose:     Implement abstract factory design pattern ( https://en.wikipedia.org/wiki/Abstract_factory_pattern ) to
 *                  a) encourage encapsulation in the architecture and
 *                  b) make mocking in unit tests easier
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#ifndef DECODE_ORC_ROOT_STAGE_FACTORIES_INTERFACE_H
#define DECODE_ORC_ROOT_STAGE_FACTORIES_INTERFACE_H

#include <memory>

#include "daphne_vbi_sink_stage_deps_interface.h"
#include "triggerable_stage.h"

namespace orc
{
    /**
     * The reason we need this separate factory for stages (instead of shoving everything into IFactories) is that the stages are created somewhat dynamically and are like plugins.
     * Therefore, each stage should know about its dependencies with the parent 'core' being intentionally ignorant, to promote encapsulation.
     *
     * We could take this a step further and have separate factories for each stage, but that introduces a lot of boilerplate.
     * We could also just shove everything into IFactories which introduces a lot of bloat in one interface.
     *
     * Having a separate factory for all stages is a decent compromise.
     */
    class IStageFactories
    {
    public:
        virtual ~IStageFactories() = default;

        /*
         * Architectural strategy:
         *
         * Methods here should only be to create dependencies of stages.
         * Dependencies of those dependencies should be created implicitly inside the implementation of StageFactories to follow Dependency Inversion design pattern.
         *
         */

        virtual std::shared_ptr<IDaphneVBISinkStageDeps> CreateInstanceDaphneVBISinkStageDeps(TriggerProgressCallback progress_callback, std::atomic<bool> *pIsProcessing, std::atomic<bool> *pCancelRequested) = 0;

    };
}
#endif //DECODE_ORC_ROOT_STAGE_FACTORIES_INTERFACE_H