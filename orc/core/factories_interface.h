/*
* File:        factories_interface.h
 * Module:      orc-core
 * Purpose:     Implement abstract factory design pattern ( https://en.wikipedia.org/wiki/Abstract_factory_pattern ) to
 *                  a) encourage encapsulation in the architecture and
 *                  b) make mocking in unit tests easier
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#ifndef DECODE_ORC_ROOT_FACTORIES_INTERFACE_H
#define DECODE_ORC_ROOT_FACTORIES_INTERFACE_H
#include <memory>

#include "include/file_io_interface.h"
#include "stages/stage_factories_interface.h"

namespace orc
{

 class IStageFactories;

 /**
  * @brief Interface containing abstract factory methods.
  *
  * Purpose: To increase encapsulation and testability.
  */
 class IFactories
 {
 public:

  virtual ~IFactories() = default;

  /**
   * @brief Gets instance of stage factories singleton.
   *
   * Returning a ptr here instead of a unique_ptr because this object is a singleton that is only instantiated once.
   * @return Instance of IStageFactories
   */
  virtual IStageFactories* get_instance_stage_factories() = 0;

  /*
   * Factory methods for BufferedFileWriter.
   * Since template methods cannot be virtual, we must define factory methods for each type that we need.
   */

  virtual std::shared_ptr<IFileWriter<uint8_t>> create_instance_buffered_file_writer_uint8(size_t buffer_size) = 0;
  virtual std::shared_ptr<IFileWriter<uint16_t>> create_instance_buffered_file_writer_uint16(size_t buffer_size) = 0;
 };
} // orc

#endif //DECODE_ORC_ROOT_FACTORIES_INTERFACE_H