/*
 * File:        stage_plugin_loader.h
 * Module:      orc-core
 * Purpose:     Runtime stage plugin loading contract and loader
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#if defined(ORC_GUI_BUILD)
#error "GUI code cannot include core/include/stage_plugin_loader.h. Use ProjectPresenter for plugin-aware stage access."
#endif
#if defined(ORC_CLI_BUILD)
#error "CLI code cannot include core/include/stage_plugin_loader.h. Use ProjectPresenter for plugin-aware stage access."
#endif

// The public plugin SDK header is the canonical source for the ABI contract
// (StagePluginDescriptor, versioning constants, OrcStageFactoryFn, etc.).
// Internal orc-core code includes it via relative path; plugin authors use
// <orc/plugin/orc_plugin_abi.h> or the umbrella <orc/plugin/orc_plugin_sdk.h>.
#include "../../sdk/include/orc/plugin/orc_plugin_abi.h"

#include "dag_executor.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace orc {

struct LoadedStagePlugin {
    std::string path;
    std::string plugin_id;
    std::string plugin_version;
    std::string license_spdx;
    bool is_core_plugin = false;
    std::vector<std::string> registered_stage_names;
};

class StagePluginLoader {
public:
    using RegisterStageFactory = std::function<DAGStagePtr()>;
    using RegisterStageCallback = std::function<bool(const std::string& stage_name, RegisterStageFactory factory)>;

    struct LoadResult {
        bool success = false;
        std::string error_message;
        std::optional<LoadedStagePlugin> plugin;
    };

    StagePluginLoader() = default;
    ~StagePluginLoader();

    StagePluginLoader(const StagePluginLoader&) = delete;
    StagePluginLoader& operator=(const StagePluginLoader&) = delete;

    LoadResult load_plugin(const std::string& path, const RegisterStageCallback& register_stage_callback);
    void unload_all();

    const std::vector<LoadedStagePlugin>& loaded_plugins() const { return loaded_plugins_; }

private:
    struct PluginHandleEntry {
        void* handle = nullptr;
        LoadedStagePlugin plugin;
    };

    static bool register_stage_trampoline(void* context, const char* stage_name, OrcStageFactoryFn factory);
    static std::string sanitize_c_string(const char* value);

    std::vector<PluginHandleEntry> handle_entries_;
    std::vector<LoadedStagePlugin> loaded_plugins_;
};

} // namespace orc
