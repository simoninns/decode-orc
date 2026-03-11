/*
* File:        factories.h
 * Module:      orc-core
 * Purpose:     Implement abstract factory design pattern ( https://en.wikipedia.org/wiki/Abstract_factory_pattern ) to
 *                  a) encourage encapsulation in the architecture and
 *                  b) make mocking in unit tests easier
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#include "factories.h"
#include "buffered_file_io.h"

namespace orc
{
    IStageFactories* Factories::get_instance_stage_factories()
    {
        return factoriesStage_.get();
    }

    std::shared_ptr<IFileWriter<uint8_t>> Factories::create_instance_buffered_file_writer_uint8(size_t buffer_size)
    {
        return std::make_shared<BufferedFileWriter<uint8_t>>(buffer_size);
    }

    std::shared_ptr<IFileWriter<uint16_t>> Factories::create_instance_buffered_file_writer_uint16(size_t buffer_size)
    {
        return std::make_shared<BufferedFileWriter<uint16_t>>(buffer_size);
    }
}
