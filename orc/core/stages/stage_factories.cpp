/*
* File:        stage_factories.cpp
 * Module:      orc-core
 * Purpose:     Implement abstract factory design pattern ( https://en.wikipedia.org/wiki/Abstract_factory_pattern ) to
 *                  a) encourage encapsulation in the architecture and
 *                  b) make mocking in unit tests easier
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#include "stage_factories.h"

#include "daphne_vbi_sink_stage_deps.h"
#include "daphne_vbi_writer_util.h"

namespace orc
{

    std::shared_ptr<IDaphneVBISinkStageDeps> StageFactories::CreateInstanceDaphneVBISinkStageDeps(TriggerProgressCallback &progress_callback, std::atomic<bool> &is_processing, std::atomic<bool> &cancel_requested)
    {
        // We follow the Dependency Inversion pattern by giving our IDaphneVBISinkStageDeps instance all of its dependencies here, rather
        //   than making the caller have to know where to get them.
        // We use shared_ptr instead of just passing normal pointers so that we don't have to manage the memory for these created objects.
        auto instance = std::make_shared<DaphneVBISinkStageDeps>(factories_, *this);
        instance->init(progress_callback, &is_processing, &cancel_requested);
        return instance;
    }

    std::shared_ptr<IDaphneVBIWriterUtil> StageFactories::CreateInstanceDaphneVBIWriterUtil(
	    IFileWriter<uint8_t>& writer)
    {
    	auto instanceWriterUtil = std::make_shared<DaphneVBIWriterUtil>();
    	instanceWriterUtil->init(&writer);
    	return instanceWriterUtil;
    }
} // orc
