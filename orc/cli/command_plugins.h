/*
 * File:        command_plugins.h
 * Module:      orc-cli
 * Purpose:     Plugin registry management subcommand header
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

namespace orc {
namespace cli {

/**
 * @brief Entry point for the 'plugins' subcommand group
 *
 * Handles plugin registry management commands:
 *   list               - Print all registry entries and loaded plugins
 *   add <path> ...     - Register a new plugin in the persistent registry
 *   remove <id>        - Remove a plugin from the persistent registry
 *   enable <id>        - Enable a registered plugin
 *   disable <id>       - Disable a registered plugin
 *
 * @param argc Argument count (argv[0] == "plugins")
 * @param argv Argument values
 * @return Exit code (0 = success, non-zero = error)
 */
int plugins_command(int argc, char* argv[]);

}  // namespace cli
}  // namespace orc
