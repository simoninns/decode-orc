/*
 * File:        stage_plugin_loader.cpp
 * Module:      orc-core
 * Purpose:     Runtime stage plugin loading contract and loader
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "include/stage_plugin_loader.h"
#include "include/plugin_safe_call.h"
#include "include/colour_preview_conversion.h"
#include "../../sdk/include/orc/plugin/orc_plugin_services.h"
#include "../../sdk/include/orc/plugin/orc_stage_services.h"
#include "factories.h"
#include "include/file_io_interface.h"
#include "../common/include/logging.h"

#include <cstring>
#include <fmt/format.h>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace orc {
using orc::core_internal::plugin_safe_call;

namespace {

class FileWriterUint8ServiceAdapter final : public IFileWriterUint8 {
public:
    explicit FileWriterUint8ServiceAdapter(std::shared_ptr<IFileWriter<uint8_t>> writer)
        : writer_(std::move(writer)) {}

    bool open(const std::string& filepath) override
    {
        return writer_ && writer_->open(filepath);
    }

    void write(const uint8_t* data, size_t count) override
    {
        if (writer_) {
            writer_->write(data, count);
        }
    }

    void write(const std::vector<uint8_t>& data) override
    {
        if (writer_) {
            writer_->write(data);
        }
    }

    void flush() override
    {
        if (writer_) {
            writer_->flush();
        }
    }

    void close() override
    {
        if (writer_) {
            writer_->close();
        }
    }

private:
    std::shared_ptr<IFileWriter<uint8_t>> writer_;
};

class FileWriterUint16ServiceAdapter final : public IFileWriterUint16 {
public:
    explicit FileWriterUint16ServiceAdapter(std::shared_ptr<IFileWriter<uint16_t>> writer)
        : writer_(std::move(writer)) {}

    bool open(const std::string& filepath) override
    {
        return writer_ && writer_->open(filepath);
    }

    void write(const uint16_t* data, size_t count) override
    {
        if (writer_) {
            writer_->write(data, count);
        }
    }

    void write(const std::vector<uint16_t>& data) override
    {
        if (writer_) {
            writer_->write(data);
        }
    }

    void flush() override
    {
        if (writer_) {
            writer_->flush();
        }
    }

    void close() override
    {
        if (writer_) {
            writer_->close();
        }
    }

private:
    std::shared_ptr<IFileWriter<uint16_t>> writer_;
};

class CoreStageServicesAdapter final : public IStageServices {
public:
    std::shared_ptr<IFileWriterUint8> create_buffered_file_writer_uint8(size_t buffer_size) override
    {
        auto factories = Factories::instance();
        if (!factories) {
            return nullptr;
        }
        auto writer = factories->create_instance_buffered_file_writer_uint8(buffer_size);
        if (!writer) {
            return nullptr;
        }
        return std::make_shared<FileWriterUint8ServiceAdapter>(std::move(writer));
    }

    std::shared_ptr<IFileWriterUint16> create_buffered_file_writer_uint16(size_t buffer_size) override
    {
        auto factories = Factories::instance();
        if (!factories) {
            return nullptr;
        }
        auto writer = factories->create_instance_buffered_file_writer_uint16(buffer_size);
        if (!writer) {
            return nullptr;
        }
        return std::make_shared<FileWriterUint16ServiceAdapter>(std::move(writer));
    }
};

struct RegisterContext {
    const StagePluginLoader::RegisterStageCallback* callback = nullptr;
    LoadedStagePlugin* plugin = nullptr;
    std::string* last_error = nullptr;
};

void* open_shared_library(const std::string& path, std::string& error_message)
{
#if defined(_WIN32)
    HMODULE handle = LoadLibraryA(path.c_str());
    if (!handle) {
        error_message = "LoadLibrary failed";
        return nullptr;
    }
    return reinterpret_cast<void*>(handle);
#else
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        const char* error = dlerror();
        error_message = error ? error : "dlopen failed";
        return nullptr;
    }
    return handle;
#endif
}

void close_shared_library(void* handle)
{
    if (!handle) {
        return;
    }

#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

void* get_symbol(void* handle, const char* symbol_name)
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), symbol_name));
#else
    return dlsym(handle, symbol_name);
#endif
}

} // namespace

StagePluginLoader::~StagePluginLoader()
{
    unload_all();
}

StagePluginLoader::LoadResult StagePluginLoader::load_plugin(
    const std::string& path,
    const RegisterStageCallback& register_stage_callback)
{
    LoadResult result;

    std::string open_error;
    void* handle = open_shared_library(path, open_error);
    if (!handle) {
        result.error_message = "Failed to open plugin '" + path + "': " + open_error;
        return result;
    }

    auto* descriptor_fn = reinterpret_cast<OrcGetStagePluginDescriptorFn>(
        get_symbol(handle, kGetStagePluginDescriptorSymbol));
    auto* register_fn = reinterpret_cast<OrcRegisterStagePluginFn>(
        get_symbol(handle, kRegisterStagePluginSymbol));

    if (!descriptor_fn || !register_fn) {
        close_shared_library(handle);
        result.error_message = "Plugin '" + path + "' is missing required stage plugin entrypoints";
        return result;
    }

    const StagePluginDescriptor* descriptor = nullptr;
    std::string fault_error;
    if (!plugin_safe_call([&]{ descriptor = descriptor_fn(); }, fault_error)) {
        close_shared_library(handle);
        result.error_message = "Plugin '" + path + "' crashed in descriptor function: " + fault_error;
        return result;
    }
    if (!descriptor) {
        close_shared_library(handle);
        result.error_message = "Plugin '" + path + "' returned null descriptor";
        return result;
    }

    // Copy version values from descriptor BEFORE any close_shared_library() call.
    // After dlclose(), the memory is unmapped and descriptor would be dangling.
    const uint32_t plugin_host_abi = descriptor->host_abi_version;
    const uint32_t plugin_api      = descriptor->plugin_api_version;

    if (plugin_host_abi != kStagePluginHostAbiVersion) {
        close_shared_library(handle);
        result.error_message =
            "Plugin '" + path + "' ABI mismatch: plugin=" + std::to_string(plugin_host_abi) +
            ", host=" + std::to_string(kStagePluginHostAbiVersion);
        return result;
    }

    if (plugin_api != kStagePluginApiVersion) {
        close_shared_library(handle);
        result.error_message =
            "Plugin '" + path + "' API version mismatch: plugin=" + std::to_string(plugin_api) +
            ", host=" + std::to_string(kStagePluginApiVersion);
        return result;
    }

    PluginHandleEntry entry;
    entry.handle = handle;
    entry.plugin.path = path;
    entry.plugin.plugin_id = sanitize_c_string(descriptor->plugin_id);
    entry.plugin.plugin_version = sanitize_c_string(descriptor->plugin_version);
    entry.plugin.license_spdx = sanitize_c_string(descriptor->license_spdx);
    entry.plugin.is_core_plugin = descriptor->is_core_plugin;

    // Build the service table that the plugin receives as the first argument to
    // orc_register_stage_plugin().  Callbacks wrap host-internal functions via
    // stateless function pointers so that no host-internal symbols are resolved
    // directly via the dynamic linker from within the plugin.
    OrcPluginServices services{};
    static CoreStageServicesAdapter stage_services_adapter;
    services.services_size = static_cast<uint32_t>(sizeof(OrcPluginServices));
    services.log = [](OrcPluginLogLevel level, const char* message) {
        auto logger = orc::get_app_logger();
        if (!logger || !message) { return; }
        switch (level) {
            case OrcPluginLogLevel::Trace:    SPDLOG_LOGGER_TRACE(logger, "{}", message);    break;
            case OrcPluginLogLevel::Debug:    SPDLOG_LOGGER_DEBUG(logger, "{}", message);    break;
            case OrcPluginLogLevel::Info:     SPDLOG_LOGGER_INFO(logger, "{}", message);     break;
            case OrcPluginLogLevel::Warn:     SPDLOG_LOGGER_WARN(logger, "{}", message);     break;
            case OrcPluginLogLevel::Error:    SPDLOG_LOGGER_ERROR(logger, "{}", message);    break;
            case OrcPluginLogLevel::Critical: SPDLOG_LOGGER_CRITICAL(logger, "{}", message); break;
        }
    };
    services.render_colour_preview = [](const ColourFrameCarrier* carrier) -> PreviewImage {
        if (!carrier) { return PreviewImage{}; }
        return render_preview_from_colour_carrier(*carrier);
    };
    services.stage_services = &stage_services_adapter;

    std::string last_error;
    RegisterContext context{&register_stage_callback, &entry.plugin, &last_error};
    const char* plugin_error = nullptr;

    bool register_ok = false;
    if (!plugin_safe_call([&]{ register_ok = register_fn(&services, &context, &StagePluginLoader::register_stage_trampoline, &plugin_error); }, fault_error)) {
        close_shared_library(handle);
        result.error_message = "Plugin '" + path + "' crashed during stage registration: " + fault_error;
        return result;
    }
    if (!register_ok) {
        close_shared_library(handle);
        std::string error_from_plugin = sanitize_c_string(plugin_error);
        if (!last_error.empty()) {
            result.error_message = "Plugin '" + path + "' stage registration failed: " + last_error;
        } else if (!error_from_plugin.empty()) {
            result.error_message = "Plugin '" + path + "' stage registration failed: " + error_from_plugin;
        } else {
            result.error_message = "Plugin '" + path + "' stage registration failed";
        }
        return result;
    }

    loaded_plugins_.push_back(entry.plugin);
    handle_entries_.push_back(std::move(entry));

    result.success = true;
    result.plugin = loaded_plugins_.back();
    return result;
}

void StagePluginLoader::unload_all()
{
    for (auto& entry : handle_entries_) {
        close_shared_library(entry.handle);
        entry.handle = nullptr;
    }
    handle_entries_.clear();
    loaded_plugins_.clear();
}

bool StagePluginLoader::register_stage_trampoline(void* context, const char* stage_name, OrcStageFactoryFn factory)
{
    auto* register_context = reinterpret_cast<RegisterContext*>(context);
    if (!register_context || !register_context->callback || !factory || !stage_name) {
        if (register_context && register_context->last_error) {
            *register_context->last_error = "invalid stage registration callback payload";
        }
        return false;
    }

    StagePluginLoader::RegisterStageFactory wrapped_factory = [factory]() {
        return factory();
    };

    const std::string stage_name_string(stage_name);
    const bool ok = (*register_context->callback)(stage_name_string, wrapped_factory);
    if (!ok) {
        if (register_context->last_error) {
            *register_context->last_error = "host rejected stage '" + stage_name_string + "'";
        }
        return false;
    }

    if (register_context->plugin) {
        register_context->plugin->registered_stage_names.push_back(stage_name_string);
    }

    return true;
}

std::string StagePluginLoader::sanitize_c_string(const char* value)
{
    if (!value) {
        return "";
    }
    // Use strnlen to guard against non-null-terminated strings from untrusted
    // plugin binaries.  512 characters is well beyond any legitimate field
    // value; anything longer is truncated rather than letting the read run
    // off into unmapped memory.
    constexpr std::size_t kMaxLen = 512;
    return std::string(value, strnlen(value, kMaxLen));
}

} // namespace orc
