/*
 * File:        crash_handler.h
 * Module:      orc-core
 * Purpose:     Crash handler for creating diagnostic bundles
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef CRASH_HANDLER_H
#define CRASH_HANDLER_H

#include <string>
#include <functional>

namespace orc {

/**
 * @brief Configuration options for crash handler
 * 
 * This structure contains all configuration options for initializing the crash handler system.
 * The crash handler automatically creates diagnostic bundles when the application crashes,
 * containing system information, backtraces, logs, and coredumps to aid in debugging.
 * 
 * @par Example Usage:
 * @code
 * orc::CrashHandlerConfig config;
 * config.application_name = "orc-gui";
 * config.version = ORC_VERSION;
 * config.output_directory = "/home/user/crashes";
 * config.custom_info_callback = []() {
 *     return "Current project: " + get_project_name();
 * };
 * orc::init_crash_handler(config);
 * @endcode
 * 
 * @see init_crash_handler()
 */
struct CrashHandlerConfig {
    std::string application_name;     ///< Name of the application (e.g., "orc-gui", "orc-cli")
    std::string version;              ///< Application version string (git commit hash, etc.)
    std::string output_directory;     ///< Directory to save crash bundles (default: current directory)
    bool enable_coredump;             ///< Whether to enable and include coredumps (default: true on Linux)
    bool auto_upload_info;            ///< Whether to show GitHub issue upload instructions (default: true)
    
    /// Optional callback for collecting additional application-specific data
    /// @return String containing custom application state to include in crash report
    std::function<std::string()> custom_info_callback;
    
    /// Constructor with sensible defaults
    CrashHandlerConfig()
        : application_name("orc")
        , version("unknown")
        , output_directory(".")
        , enable_coredump(true)
        , auto_upload_info(true)
        , custom_info_callback(nullptr)
    {}
};

/**
 * @brief Initialize crash handler with specified configuration
 * 
 * Sets up signal handlers for common crash signals (SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS, SIGTRAP)
 * and configures the system to generate coredumps when crashes occur. When a crash is detected, 
 * the handler will automatically create a diagnostic bundle containing:
 * 
 * - **Crash information**: Signal type, backtrace, timestamp
 * - **System information**: OS, kernel, architecture, CPU model, memory
 * - **Application data**: Version, working directory, log files
 * - **Coredump file**: If enabled and available
 * - **Custom data**: Via custom_info_callback if provided
 * 
 * The bundle is saved as `crash_bundle_YYYYMMDD_HHMMSS.zip` in the configured output directory
 * and includes a README with instructions for reporting issues on GitHub.
 * 
 * @note This function should be called once during application startup, after logging initialization.
 * @note On Linux, this sets core file size limits to unlimited to enable coredump generation.
 * @note Signal handlers use SA_RESETHAND flag (one-shot) to prevent recursive crashes.
 * 
 * @param config Configuration options for the crash handler
 * @return true if initialization was successful, false if already initialized or on error
 * 
 * @par Thread Safety:
 * Not thread-safe. Must be called from main thread before starting other threads.
 * 
 * @par Example:
 * @code
 * orc::CrashHandlerConfig crash_config;
 * crash_config.application_name = "my-app";
 * crash_config.version = "1.0.0-abc123";
 * crash_config.output_directory = "/var/crashes";
 * 
 * if (!orc::init_crash_handler(crash_config)) {
 *     std::cerr << "Failed to initialize crash handler\n";
 * }
 * @endcode
 * 
 * @see CrashHandlerConfig
 * @see cleanup_crash_handler()
 */
bool init_crash_handler(const CrashHandlerConfig& config);

/**
 * @brief Get the path to the most recent crash bundle
 * 
 * Returns the filesystem path to the last crash bundle created by either the signal
 * handler or create_crash_bundle(). This can be used to programmatically access
 * the crash data after a crash was handled.
 * 
 * @return Path to the most recent crash bundle zip file, or empty string if none exists
 * 
 * @par Example:
 * @code
 * std::string bundle = orc::get_last_crash_bundle_path();
 * if (!bundle.empty()) {
 *     std::cout << "Last crash: " << bundle << "\n";
 * }
 * @endcode
 */
std::string get_last_crash_bundle_path();

/**
 * @brief Manually trigger crash bundle creation
 * 
 * Creates a diagnostic bundle without waiting for a crash signal. This can be called
 * from exception handlers or other error paths to create a bundle for non-fatal errors
 * or to test the crash handling system.
 * 
 * The bundle will contain the same information as a signal-triggered bundle, but with
 * the provided custom error message instead of a signal number.
 * 
 * @param error_message Custom error message to include in the bundle (e.g., exception message)
 * @return Path to the created bundle zip file, or empty string on failure
 * 
 * @par Example:
 * @code
 * try {
 *     dangerous_operation();
 * } catch (const std::exception& e) {
 *     std::string bundle = orc::create_crash_bundle(
 *         std::string("Exception: ") + e.what()
 *     );
 *     if (!bundle.empty()) {
 *         std::cerr << "Crash bundle: " << bundle << "\n";
 *     }
 *     throw;
 * }
 * @endcode
 * 
 * @see init_crash_handler()
 */
std::string create_crash_bundle(const std::string& error_message);

/**
 * @brief Clean up crash handler resources
 * 
 * Restores the original signal handlers that were replaced during init_crash_handler().
 * Should be called before application exit to ensure clean shutdown.
 * 
 * @note Safe to call even if crash handler was not initialized.
 * @note After calling this, crashes will no longer generate diagnostic bundles.
 * 
 * @par Example:
 * @code
 * int main() {
 *     orc::init_crash_handler(config);
 *     
 *     // ... application code ...
 *     
 *     orc::cleanup_crash_handler();
 *     return 0;
 * }
 * @endcode
 * 
 * @see init_crash_handler()
 */
void cleanup_crash_handler();

} // namespace orc

#endif // CRASH_HANDLER_H
