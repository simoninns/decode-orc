/*
 * File:        gui_test_logging_stub.cpp
 * Module:      orc-tests/gui/unit
 * Purpose:     Minimal GUI logger shim for tests linking orc-gui-lib
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "logging.h"

namespace orc {

std::shared_ptr<spdlog::logger> get_gui_logger() {
  return spdlog::default_logger();
}

void reset_gui_logger() {}

void init_gui_logging(const std::string&, const std::string&,
                      const std::string&) {}

}  // namespace orc