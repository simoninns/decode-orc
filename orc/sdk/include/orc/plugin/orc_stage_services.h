/*
 * File:        orc_stage_services.h
 * Module:      decode-orc Plugin SDK
 * Purpose:     Stable stage service interfaces exposed via OrcPluginServices
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 *
 * STABILITY: PUBLIC — This header is part of the stable plugin SDK.
 *
 * NOTE:
 *   These interfaces intentionally consolidate similar host capability entry
 *   points into one service family so plugin authors can depend on one
 *   canonical abstraction per requirement.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace orc {

class IFileWriterUint8 {
 public:
  virtual ~IFileWriterUint8() = default;
  virtual bool open(const std::string& filepath) = 0;
  virtual void write(const uint8_t* data, size_t count) = 0;
  virtual void write(const std::vector<uint8_t>& data) = 0;
  virtual void flush() = 0;
  virtual void close() = 0;
};

class IFileWriterUint16 {
 public:
  virtual ~IFileWriterUint16() = default;
  virtual bool open(const std::string& filepath) = 0;
  virtual void write(const uint16_t* data, size_t count) = 0;
  virtual void write(const std::vector<uint16_t>& data) = 0;
  virtual void flush() = 0;
  virtual void close() = 0;
};

class IStageServices {
 public:
  virtual ~IStageServices() = default;

  // Canonical sink/file output factory entry points.
  virtual std::shared_ptr<IFileWriterUint8> create_buffered_file_writer_uint8(
      size_t buffer_size) = 0;
  virtual std::shared_ptr<IFileWriterUint16> create_buffered_file_writer_uint16(
      size_t buffer_size) = 0;
};

}  // namespace orc
