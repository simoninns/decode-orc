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

#ifndef DECODE_ORC_ROOT_FACTORIES_H
#define DECODE_ORC_ROOT_FACTORIES_H
#include "factories_interface.h"
#include "stages/stage_factories.h"

namespace orc
{
    /**
     * @brief Concrete implementation of IFactories interface.
     *
     * Purpose: Increase encapsulation and testability
     */
    class Factories : public IFactories
    {
    public:

        Factories()
        {
            // we want this to be a singleton because it may be used multiple times and there's no need for multiple instances.
            factoriesStage_ = std::make_unique<StageFactories>(this);
        }

        IStageFactories* get_instance_stage_factories() override;

        /**
         * @brief Create instance of BufferedFileWriter<uint8_t>
         */
        std::shared_ptr<IFileWriter<uint8_t>> create_instance_buffered_file_writer_uint8(size_t buffer_size) override;

        /**
         * @brief Create instance of BufferedFileWriter<uint16_t>
         */
        std::shared_ptr<IFileWriter<uint16_t>> create_instance_buffered_file_writer_uint16(size_t buffer_size) override;

    private:
        std::unique_ptr<IStageFactories> factoriesStage_;
    };
} // orc

#endif //DECODE_ORC_ROOT_FACTORIES_H