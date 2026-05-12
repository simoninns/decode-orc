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

namespace orc
{

    std::shared_ptr<IDaphneVBISinkStageDeps> StageFactories::CreateInstanceDaphneVBISinkStageDeps(TriggerProgressCallback &progress_callback, std::atomic<bool> &is_processing, std::atomic<bool> &cancel_requested)
    {
        (void)progress_callback;
        (void)is_processing;
        (void)cancel_requested;
        return nullptr;
    }

    std::shared_ptr<IDaphneVBIWriterUtil> StageFactories::CreateInstanceDaphneVBIWriterUtil(
	    IFileWriter<uint8_t>& writer)
    {
	    (void)writer;
	    return nullptr;
    }

    std::shared_ptr<ILDSinkStageDeps> StageFactories::CreateInstanceLDSinkStageDeps(
        TriggerProgressCallback& progress_callback,
        std::atomic<bool>& is_processing,
        std::atomic<bool>& cancel_requested)
    {
        (void)progress_callback;
        (void)is_processing;
        (void)cancel_requested;
        return nullptr;
    }
} // orc
