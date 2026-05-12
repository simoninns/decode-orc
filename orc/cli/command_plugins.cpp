/*
 * File:        command_plugins.cpp
 * Module:      orc-cli
 * Purpose:     Plugin registry management subcommand
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "command_plugins.h"
#include "project_presenter.h"

#include <iostream>
#include <string>
#include <vector>

namespace orc {
namespace cli {

namespace {

void print_plugins_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " plugins <subcommand> [options]\n";
    std::cerr << "\n";
    std::cerr << "Subcommands:\n";
    std::cerr << "  list                           Show registry entries and loaded plugins\n";
    std::cerr << "  add <path> [options]           Add a plugin to the persistent registry\n";
    std::cerr << "  remove <id>                    Remove a plugin from the persistent registry\n";
    std::cerr << "  enable <id>                    Enable a registered plugin\n";
    std::cerr << "  disable <id>                   Disable a registered plugin\n";
    std::cerr << "\n";
    std::cerr << "Options for 'add':\n";
    std::cerr << "  --id ID                        Plugin identifier (e.g. com.example.myplugin)\n";
    std::cerr << "  --version VER                  Plugin version string\n";
    std::cerr << "  --license SPDX                 License identifier (e.g. MIT, GPL-3.0-or-later)\n";
    std::cerr << "  --trusted                      Mark plugin as trusted\n";
    std::cerr << "\n";
    std::cerr << "Note: Registry changes take effect on the next application launch.\n";
}

int cmd_plugins_list() {
    const auto registry = orc::presenters::ProjectPresenter::readPluginRegistry();
    const auto loaded   = orc::presenters::ProjectPresenter::getLoadedPlugins();

    std::cout << "Registry path: "
              << (registry.registry_path.empty() ? "<none>" : registry.registry_path)
              << "\n\n";

    if (registry.entries.empty()) {
        std::cout << "No plugins registered.\n";
    } else {
        std::cout << "Registered plugins (" << registry.entries.size() << "):\n";
        for (const auto& e : registry.entries) {
            const std::string id      = e.plugin_id.empty() ? "<unnamed>" : e.plugin_id;
            const std::string version = e.plugin_version.empty() ? "-" : e.plugin_version;
            const std::string license = e.license_spdx.empty() ? "-" : e.license_spdx;

            std::cout << "  id:       " << id << "\n";
            std::cout << "  path:     " << e.path << "\n";
            std::cout << "  version:  " << version << "\n";
            std::cout << "  license:  " << license << "\n";
            std::cout << "  enabled:  " << (e.enabled ? "yes" : "no") << "\n";
            std::cout << "  trusted:  " << (e.trust_state == "trusted" ? "yes" : "no") << "\n";
            std::cout << "  core:     " << (e.is_core_plugin ? "yes" : "no") << "\n";
            std::cout << "  exists:   " << (e.path_exists ? "yes" : "no") << "\n";
            std::cout << "  loaded:   " << (e.is_loaded ? "yes" : "no") << "\n";
            std::cout << "\n";
        }
    }

    if (!loaded.empty()) {
        std::cout << "Loaded plugins this session (" << loaded.size() << "):\n";
        for (const auto& p : loaded) {
            std::cout << "  " << p.plugin_id
                      << " v" << p.plugin_version
                      << " (" << p.registered_stage_names.size() << " stage(s))"
                      << "\n";
        }
    }

    return 0;
}

int cmd_plugins_add(int argc, char* argv[]) {
    // argv[0] = "add", argv[1] = <path>, remaining = options
    if (argc < 2) {
        std::cerr << "Error: 'add' requires a plugin path\n";
        std::cerr << "Usage: orc-cli plugins add <path> [--id ID] [--version VER] "
                     "[--license SPDX] [--trusted]\n";
        return 1;
    }

    std::string path       = argv[1];
    std::string plugin_id;
    std::string plugin_version;
    std::string license_spdx;
    bool trusted           = false;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--id" && i + 1 < argc) {
            plugin_id = argv[++i];
        } else if (arg == "--version" && i + 1 < argc) {
            plugin_version = argv[++i];
        } else if (arg == "--license" && i + 1 < argc) {
            license_spdx = argv[++i];
        } else if (arg == "--trusted") {
            trusted = true;
        } else {
            std::cerr << "Error: Unknown option: " << arg << "\n";
            return 1;
        }
    }

    const auto result = orc::presenters::ProjectPresenter::addPluginToRegistry(
        path, plugin_id, plugin_version, license_spdx, false, trusted);

    if (!result.success) {
        std::cerr << "Error: " << result.error_message << "\n";
        return 1;
    }

    std::cout << "Plugin added to registry.\n";
    std::cout << "Note: Changes take effect on the next application launch.\n";
    return 0;
}

int cmd_plugins_remove(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Error: 'remove' requires a plugin id\n";
        std::cerr << "Usage: orc-cli plugins remove <id>\n";
        return 1;
    }

    const std::string plugin_id = argv[1];
    const auto result = orc::presenters::ProjectPresenter::removePluginFromRegistry(plugin_id);

    if (!result.success) {
        std::cerr << "Error: " << result.error_message << "\n";
        return 1;
    }

    std::cout << "Plugin '" << plugin_id << "' removed from registry.\n";
    std::cout << "Note: Changes take effect on the next application launch.\n";
    return 0;
}

int cmd_plugins_set_enabled(int argc, char* argv[], bool enabled) {
    const std::string subcommand = enabled ? "enable" : "disable";
    if (argc < 2) {
        std::cerr << "Error: '" << subcommand << "' requires a plugin id\n";
        std::cerr << "Usage: orc-cli plugins " << subcommand << " <id>\n";
        return 1;
    }

    const std::string plugin_id = argv[1];
    const auto result = orc::presenters::ProjectPresenter::setPluginRegistryEntryEnabled(plugin_id, enabled);

    if (!result.success) {
        std::cerr << "Error: " << result.error_message << "\n";
        return 1;
    }

    std::cout << "Plugin '" << plugin_id << "' "
              << (enabled ? "enabled" : "disabled") << ".\n";
    std::cout << "Note: Changes take effect on the next application launch.\n";
    return 0;
}

} // namespace

int plugins_command(int argc, char* argv[]) {
    // argv[0] = "plugins"
    if (argc < 2) {
        print_plugins_usage(argv[0]);
        return 1;
    }

    const std::string subcommand = argv[1];

    if (subcommand == "--help" || subcommand == "-h") {
        print_plugins_usage(argv[0]);
        return 0;
    }

    if (subcommand == "list") {
        return cmd_plugins_list();
    }

    if (subcommand == "add") {
        // Pass remaining args starting from "add"
        return cmd_plugins_add(argc - 1, argv + 1);
    }

    if (subcommand == "remove") {
        return cmd_plugins_remove(argc - 1, argv + 1);
    }

    if (subcommand == "enable") {
        return cmd_plugins_set_enabled(argc - 1, argv + 1, true);
    }

    if (subcommand == "disable") {
        return cmd_plugins_set_enabled(argc - 1, argv + 1, false);
    }

    std::cerr << "Error: Unknown plugins subcommand: " << subcommand << "\n\n";
    print_plugins_usage(argv[0]);
    return 1;
}

} // namespace cli
} // namespace orc
