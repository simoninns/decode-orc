/*
 * File:        crash_handler.cpp
 * Module:      orc-core
 * Purpose:     Crash handler implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

/**
 * @file crash_handler.cpp
 * @brief Automatic crash detection and diagnostic bundle generation
 * 
 * This module provides comprehensive crash handling for the decode-orc applications.
 * When a crash occurs (via signal or exception), it automatically creates a ZIP bundle
 * containing diagnostic information to help identify and fix the issue.
 * 
 * @par Crash Bundle Contents:
 * - **crash_info.txt**: Crash report with signal, backtrace, system info, app state
 * - **README.txt**: Instructions for reporting issues on GitHub
 * - ***.log**: Application log files found in output directory
 * - **coredump**: Core dump file (if available and not too large)
 * 
 * @par Signal Handling:
 * Installs handlers for: SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS, SIGTRAP
 * 
 * @par Platform Support:
 * - Linux: Full support (signals, coredumps, backtraces via execinfo.h)
 * - Other Unix: Partial support (signals, limited backtrace)
 * - Windows: Not currently supported (requires platform-specific implementation)
 * 
 * @author Simon Inns
 * @date 2026
 */

#include "crash_handler.h"
#include "logging.h"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <filesystem>
#include <vector>
#include <exception>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#endif

// Unix/POSIX-specific headers (not available on Windows)
#ifndef _WIN32
#include <sys/utsname.h>
#include <unistd.h>
#include <sys/resource.h>
#endif

#if defined(__linux__) || defined(__APPLE__)
#include <execinfo.h>
#endif

#ifdef __linux__
#include <sys/sysinfo.h>
#endif

namespace fs = std::filesystem;

namespace orc {

// Global configuration and state
static CrashHandlerConfig g_crash_config;
static std::string g_last_crash_bundle;
static bool g_crash_handler_initialized = false;
#ifndef _WIN32
static struct sigaction g_old_sigactions[32];
#else
static LPTOP_LEVEL_EXCEPTION_FILTER g_previous_exception_filter = nullptr;
static std::terminate_handler g_previous_terminate_handler = nullptr;
#endif

/**
 * @brief Get current timestamp as formatted string
 * @return Timestamp string in format YYYYMMDD_HHMMSS
 * @private
 */
static std::string get_timestamp() {
    auto now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

/**
 * @brief Get system information as a formatted string
 * @return Multi-line string containing OS, kernel, CPU, memory information
 * @private
 * 
 * Collects platform-specific system information:
 * - Operating system name and kernel version (via uname)
 * - System architecture (x86_64, arm64, etc.)
 * - Hostname
 * - Total and free RAM (Linux only)
 * - CPU model name (from /proc/cpuinfo on Linux)
 */
static std::string get_system_info() {
    std::ostringstream info;
    
    info << "=== System Information ===\n\n";
    
#ifndef _WIN32
    // OS and kernel info (Unix/POSIX only)
    struct utsname uname_data;
    if (uname(&uname_data) == 0) {
        info << "OS: " << uname_data.sysname << "\n";
        info << "Kernel: " << uname_data.release << "\n";
        info << "Architecture: " << uname_data.machine << "\n";
        info << "Hostname: " << uname_data.nodename << "\n";
    }
#else
    // Windows system information
    info << "OS: Windows\n";
    // Could add more Windows-specific info here if needed
#endif
    
#ifdef __linux__
    // Memory info
    struct sysinfo sys_info;
    if (sysinfo(&sys_info) == 0) {
        info << "Total RAM: " << (sys_info.totalram * sys_info.mem_unit / 1024 / 1024) << " MB\n";
        info << "Free RAM: " << (sys_info.freeram * sys_info.mem_unit / 1024 / 1024) << " MB\n";
    }
    
    // CPU info
    std::ifstream cpuinfo("/proc/cpuinfo");
    if (cpuinfo.is_open()) {
        std::string line;
        bool found_model = false;
        while (std::getline(cpuinfo, line) && !found_model) {
            if (line.find("model name") == 0) {
                size_t colon_pos = line.find(':');
                if (colon_pos != std::string::npos) {
                    info << "CPU: " << line.substr(colon_pos + 2) << "\n";
                    found_model = true;
                }
            }
        }
    }
#endif
    
    info << "\n";
    return info.str();
}

/**
 * @brief Get backtrace information
 * @return Multi-line string with stack backtrace
 * @private
 * 
 * On Linux, uses execinfo.h backtrace() and backtrace_symbols() to capture
 * up to 256 stack frames. Each frame shows function name, offset, and address.
 * Returns "not available" message on unsupported platforms.
 */
static std::string get_backtrace() {
    std::ostringstream trace;
    trace << "=== Stack Backtrace ===\n\n";
    
#ifndef _WIN32
    void* buffer[256];
    int nptrs = backtrace(buffer, 256);
    char** strings = backtrace_symbols(buffer, nptrs);
    
    if (strings != nullptr) {
        trace << "Raw backtrace (use addr2line for source locations):\n";
        for (int i = 0; i < nptrs; i++) {
            trace << "#" << std::setw(2) << i << " " << strings[i] << "\n";
        }
        trace << "\nTo resolve addresses to source code lines, use:\n";
        trace << "  addr2line -e <binary> -f -C -p <address>\n";
        trace << "Or use gdb:\n";
        trace << "  gdb <binary> -ex 'set confirm off' -ex 'bt' -ex quit <coredump>\n";
        trace << "\n";
        trace << "Note: Binary has debug symbols (not stripped)\n";
        free(strings);
    } else {
        trace << "Unable to obtain backtrace\n";
    }
#else
    trace << "Backtrace not available on this platform\n";
#endif
    
    trace << "\n";
    return trace.str();
}

/**
 * @brief Get signal name from signal number
 * @param sig Signal number (e.g., SIGSEGV, SIGABRT)
 * @return Human-readable signal name with description
 * @private
 */
static std::string get_signal_name(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (Segmentation fault)";
        case SIGABRT: return "SIGABRT (Abort)";
        case SIGFPE: return "SIGFPE (Floating point exception)";
        case SIGILL: return "SIGILL (Illegal instruction)";
#ifndef _WIN32
        case SIGBUS: return "SIGBUS (Bus error)";
        case SIGTRAP: return "SIGTRAP (Trace/breakpoint trap)";
#endif
        default: return "Signal " + std::to_string(sig);
    }
}

/**
 * @brief Create crash info file content
 * @param signal_num Signal number that caused crash (0 for manual/exception-based bundles)
 * @param custom_message Optional custom error message
 * @return Complete crash report as formatted string
 * @private
 * 
 * Generates a comprehensive crash report including:
 * - Application name and version (including git commit hash)
 * - Crash timestamp
 * - Signal type (if signal-triggered)
 * - System information (OS, CPU, memory)
 * - Stack backtrace
 * - Custom application state (via callback)
 */
static std::string create_crash_info(int signal_num, const std::string& custom_message = "") {
    std::ostringstream info;
    
    info << "=== Crash Report ===\n\n";
    info << "Application: " << g_crash_config.application_name << "\n";
    info << "Version: " << g_crash_config.version << "\n";
    info << "Timestamp: " << get_timestamp() << "\n";
    
    if (signal_num > 0) {
        info << "Signal: " << get_signal_name(signal_num) << "\n";
    }
    
    if (!custom_message.empty()) {
        info << "Error Message: " << custom_message << "\n";
    }
    
    info << "\n";
    info << get_system_info();
    info << get_backtrace();
    
    // Custom application info
    if (g_crash_config.custom_info_callback) {
        info << "=== Application State ===\n\n";
        try {
            info << g_crash_config.custom_info_callback() << "\n";
        } catch (...) {
            info << "Error collecting custom application info\n\n";
        }
    }
    
    return info.str();
}

/**
 * @brief Find and copy coredump file
 * @return Path to coredump if found, empty string otherwise
 * @private
 * 
 * Searches common coredump locations:
 * - ./core (current directory)
 * - ./core.PID (with process ID)
 * - /var/lib/systemd/coredump/core (systemd-coredump)
 * - /var/crash/ (Ubuntu apport)
 * 
 * @note Only available on Unix/POSIX systems
 */
static std::string find_coredump() {
#ifndef _WIN32
    // Common coredump locations
    std::vector<std::string> possible_paths = {
        "core",
        "core." + std::to_string(getpid()),
        "/var/lib/systemd/coredump/core",
    };
    
    // Check if apport is being used (Ubuntu)
    std::string apport_path = "/var/crash/_usr_bin_" + g_crash_config.application_name + "." + 
                             std::to_string(getuid()) + ".crash";
    possible_paths.push_back(apport_path);
    
    for (const auto& path : possible_paths) {
        if (fs::exists(path)) {
            return path;
        }
    }
#endif
    
    return "";
}

/**
 * @brief Create a ZIP file containing crash diagnostic information
 * @param crash_info_content The formatted crash report text
 * @param coredump_path Path to coredump file (empty if none)
 * @param log_files List of log file paths to include
 * @return Path to created ZIP file, or fallback text file on ZIP failure
 * @private
 * 
 * Creates a temporary directory, populates it with crash data, then uses
 * the system 'zip' command to create the bundle. If ZIP creation fails,
 * falls back to saving just crash_info.txt.
 * 
 * Bundle structure:
 * - crash_info.txt: Main crash report
 * - README.txt: Issue reporting instructions
 * - *.log: Application log files
 * - coredump: Core dump (if available and small enough)
 * - coredump_note.txt: Note if coredump couldn't be included
 */
static std::string create_bundle_zip(const std::string& crash_info_content, 
                                     const std::string& coredump_path,
                                     const std::vector<std::string>& log_files) {
    
    std::string timestamp = get_timestamp();
    std::string bundle_dir = g_crash_config.output_directory + "/crash_bundle_" + timestamp;
    std::string bundle_zip = bundle_dir + ".zip";
    
    try {
        // Create temporary directory for bundle contents
        fs::create_directories(bundle_dir);
        
        // Write crash info
        std::string crash_info_path = bundle_dir + "/crash_info.txt";
        std::ofstream crash_file(crash_info_path);
        if (crash_file.is_open()) {
            crash_file << crash_info_content;
            crash_file.close();
        }
        
        // Copy log files
        for (const auto& log_file : log_files) {
            if (fs::exists(log_file)) {
                try {
                    fs::path log_path(log_file);
                    fs::copy(log_file, bundle_dir + "/" + log_path.filename().string());
                } catch (...) {
                    // Continue even if log file copy fails
                }
            }
        }
        
        // Copy coredump if available and enabled
        if (g_crash_config.enable_coredump && !coredump_path.empty() && fs::exists(coredump_path)) {
            try {
                fs::copy(coredump_path, bundle_dir + "/coredump");
            } catch (...) {
                // Coredump might be too large or inaccessible
                std::ofstream note(bundle_dir + "/coredump_note.txt");
                note << "Coredump was found at: " << coredump_path << "\n";
                note << "but could not be included in the bundle (possibly too large or insufficient permissions).\n";
                note << "Please include it manually if needed.\n";
                note.close();
            }
        }
        
        // Create README with upload instructions
        std::ofstream readme(bundle_dir + "/README.txt");
        readme << "=== Crash Diagnostic Bundle ===\n\n";
        readme << "This bundle contains diagnostic information about a crash in " 
               << g_crash_config.application_name << ".\n\n";
        readme << "DEBUGGING INSTRUCTIONS:\n";
        readme << "------------------------\n";
        readme << "The binary has debug symbols. To analyze the crash:\n\n";
        readme << "1. Extract addresses from the backtrace in crash_info.txt\n";
        readme << "2. Use addr2line to get source locations:\n";
        readme << "     addr2line -e /path/to/" << g_crash_config.application_name 
               << " -f -C -p <address>\n\n";
        readme << "3. Or use gdb with the coredump:\n";
        readme << "     gdb /path/to/" << g_crash_config.application_name << " coredump\n";
        readme << "     (gdb) bt        # Show backtrace\n";
        readme << "     (gdb) bt full   # Show backtrace with variables\n";
        readme << "     (gdb) info registers  # Show CPU registers\n";
        readme << "     (gdb) frame N   # Select frame N from backtrace\n";
        readme << "     (gdb) list      # Show source code around that frame\n\n";
        readme << "TO REPORT THIS ISSUE:\n";
        readme << "---------------------\n";
        readme << "1. Go to https://github.com/simoninns/decode-orc/issues\n";
        readme << "2. Click 'New Issue'\n";
        readme << "3. Attach this ZIP file or upload it to a file sharing service\n";
        readme << "4. Include crash_info.txt contents in the issue description\n";
        readme << "5. Describe what you were doing when the crash occurred\n\n";
        readme << "FILES IN THIS BUNDLE:\n";
        readme << "---------------------\n";
        readme << "- crash_info.txt: System info, backtrace, and error details\n";
        readme << "- *.log: Application log files (if available)\n";
        readme << "- coredump: Core dump file (if available, use with gdb)\n";
        readme.close();
        
        // Create ZIP file using system zip command
#ifdef _WIN32
        // Windows: Use PowerShell's Compress-Archive (built-in on Windows 10+)
        std::string zip_command = "powershell -Command \"Compress-Archive -Path '" + bundle_dir + 
                                 "' -DestinationPath '" + bundle_zip + "' -Force\"";
#else
        // Unix/Linux: Use standard zip command with shell commands
        std::string zip_command = "cd " + g_crash_config.output_directory + 
                                 " && zip -r -q crash_bundle_" + timestamp + ".zip crash_bundle_" + timestamp;
#endif
        int result = system(zip_command.c_str());

        if (result == 0 && fs::exists(bundle_zip)) {
            // Clean up temporary directory only when ZIP was created successfully
            fs::remove_all(bundle_dir);
            return bundle_zip;
        }

        // ZIP failed; preserve uncompressed bundle directory as reliable fallback
        std::ofstream zip_failure_note(bundle_dir + "/ZIP_FAILURE.txt");
        if (zip_failure_note.is_open()) {
            zip_failure_note << "Failed to create ZIP archive from crash bundle directory.\n";
            zip_failure_note << "The uncompressed bundle directory contains complete diagnostics.\n";
            zip_failure_note << "Attempted ZIP output path: " << bundle_zip << "\n";
            zip_failure_note << "System command exit code: " << result << "\n";
            zip_failure_note.close();
        }
        return bundle_dir;
    } catch (const std::exception&) {
        // If an exception occurs and bundle directory exists, preserve it as fallback
        if (fs::exists(bundle_dir)) {
            std::ofstream exception_note(bundle_dir + "/ZIP_EXCEPTION.txt");
            if (exception_note.is_open()) {
                exception_note << "Exception while creating ZIP archive.\n";
                exception_note << "The uncompressed bundle directory contains complete diagnostics.\n";
                exception_note.close();
            }
            return bundle_dir;
        }

        // Last-resort fallback: save crash info only
        std::string fallback_path = g_crash_config.output_directory + "/crash_info_" + timestamp + ".txt";
        std::ofstream fallback(fallback_path);
        fallback << crash_info_content;
        fallback.close();
        return fallback_path;
    }
    
    return "";
}

#ifdef _WIN32
static std::string create_windows_minidump(EXCEPTION_POINTERS* exception_pointers) {
    std::string timestamp = get_timestamp();
    std::string minidump_path = g_crash_config.output_directory + "/minidump_" + timestamp + ".dmp";

    HANDLE dump_file = CreateFileA(minidump_path.c_str(),
                                   GENERIC_WRITE,
                                   FILE_SHARE_READ,
                                   nullptr,
                                   CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL,
                                   nullptr);
    if (dump_file == INVALID_HANDLE_VALUE) {
        return "";
    }

    MINIDUMP_EXCEPTION_INFORMATION exception_info{};
    MINIDUMP_EXCEPTION_INFORMATION* exception_info_ptr = nullptr;
    if (exception_pointers != nullptr) {
        exception_info.ThreadId = GetCurrentThreadId();
        exception_info.ExceptionPointers = exception_pointers;
        exception_info.ClientPointers = FALSE;
        exception_info_ptr = &exception_info;
    }

    const MINIDUMP_TYPE dump_type = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithIndirectlyReferencedMemory |
        MiniDumpWithThreadInfo |
        MiniDumpWithDataSegs
    );

    const BOOL write_result = MiniDumpWriteDump(GetCurrentProcess(),
                                                GetCurrentProcessId(),
                                                dump_file,
                                                dump_type,
                                                exception_info_ptr,
                                                nullptr,
                                                nullptr);

    CloseHandle(dump_file);

    if (write_result == FALSE) {
        fs::remove(minidump_path);
        return "";
    }

    return minidump_path;
}

static std::string create_windows_crash_bundle(const std::string& error_message,
                                               EXCEPTION_POINTERS* exception_pointers) {
    std::string crash_info = create_crash_info(0, error_message);
    std::string minidump_path = create_windows_minidump(exception_pointers);

    std::vector<std::string> log_files;
    try {
        for (const auto& entry : fs::directory_iterator(g_crash_config.output_directory)) {
            if (entry.path().extension() == ".log") {
                log_files.push_back(entry.path().string());
            }
        }
    } catch (...) {
    }

    std::string bundle_path = create_bundle_zip(crash_info, minidump_path, log_files);
    g_last_crash_bundle = bundle_path;
    return bundle_path;
}

static LONG WINAPI crash_exception_handler(EXCEPTION_POINTERS* exception_pointers) {
    static volatile LONG handling_crash = 0;
    if (InterlockedExchange(&handling_crash, 1) != 0) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    std::ostringstream message;
    message << "Unhandled Windows exception";
    if (exception_pointers != nullptr && exception_pointers->ExceptionRecord != nullptr) {
        message << " (code=0x" << std::hex << std::uppercase
                << exception_pointers->ExceptionRecord->ExceptionCode << std::dec << ")";
    }

    auto logger = get_logger();
    if (logger) {
        logger->critical("CRASH DETECTED: {}", message.str());
        logger->flush();
    }

    create_windows_crash_bundle(message.str(), exception_pointers);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void crash_terminate_handler() {
    static bool handling_terminate = false;
    if (handling_terminate) {
        std::_Exit(1);
    }
    handling_terminate = true;

    std::string message = "std::terminate called";
    try {
        std::exception_ptr current_exception = std::current_exception();
        if (current_exception) {
            std::rethrow_exception(current_exception);
        }
    } catch (const std::exception& exception) {
        message += std::string(": ") + exception.what();
    } catch (...) {
        message += " (non-std exception)";
    }

    auto logger = get_logger();
    if (logger) {
        logger->critical("CRASH DETECTED: {}", message);
        logger->flush();
    }

    create_windows_crash_bundle(message, nullptr);

    if (g_previous_terminate_handler) {
        g_previous_terminate_handler();
    }
    std::abort();
}
#endif

/**
 * @brief Signal handler for crashes
 * @param sig Signal number
 * @param info Signal information structure
 * @param context Signal context (unused)
 * @private
 * 
 * This is the actual signal handler installed for crash signals. When triggered:
 * 1. Prevents recursive crashes with static flag
 * 2. Attempts to log crash via logging system
 * 3. Collects crash info, system state, and logs
 * 4. Searches for coredump
 * 5. Creates ZIP bundle
 * 6. Prints crash message to stderr
 * 7. Re-raises signal or calls original handler
 * 
 * @note Uses write() for stderr output (async-signal-safe)
 * @note Handler is one-shot (SA_RESETHAND) to prevent infinite loops
 * @note Only available on Unix/POSIX systems
 */
#ifndef _WIN32
static void crash_signal_handler(int sig, siginfo_t* info, void* context) {
    // Prevent recursive crashes
    static volatile sig_atomic_t handling_crash = 0;
    if (handling_crash) {
        _exit(1);
    }
    handling_crash = 1;
    
    // Try to log the crash
    try {
        auto logger = get_logger();
        if (logger) {
            logger->critical("CRASH DETECTED: {}", get_signal_name(sig));
            logger->flush();
        }
    } catch (...) {
        // Logging might not work in crash handler
    }
    
    // Create crash bundle
    std::string crash_info = create_crash_info(sig);
    std::string coredump_path = find_coredump();
    
    // Try to find log files
    std::vector<std::string> log_files;
    for (const auto& entry : fs::directory_iterator(g_crash_config.output_directory)) {
        if (entry.path().extension() == ".log") {
            log_files.push_back(entry.path().string());
        }
    }
    
    std::string bundle_path = create_bundle_zip(crash_info, coredump_path, log_files);
    g_last_crash_bundle = bundle_path;
    
    // Print message to stderr
    if (!bundle_path.empty()) {
        // Use write() on Unix (async-signal-safe)
        const char* msg1 = "\n\n==================================================\n";
        const char* msg2 = "CRASH DETECTED - Diagnostic bundle created:\n";
        const char* msg3 = "\n==================================================\n\n";
        const char* msg4 = "Please report this issue at:\n";
        const char* msg5 = "https://github.com/simoninns/decode-orc/issues\n\n";
        
        if (write(STDERR_FILENO, msg1, strlen(msg1)) < 0) {}
        if (write(STDERR_FILENO, msg2, strlen(msg2)) < 0) {}
        if (write(STDERR_FILENO, bundle_path.c_str(), bundle_path.length()) < 0) {}
        if (write(STDERR_FILENO, msg3, strlen(msg3)) < 0) {}
        if (g_crash_config.auto_upload_info) {
            if (write(STDERR_FILENO, msg4, strlen(msg4)) < 0) {}
            if (write(STDERR_FILENO, msg5, strlen(msg5)) < 0) {}
        }
    }
    
    // Call original handler if it exists (or abort)
    if (g_old_sigactions[sig].sa_handler != SIG_DFL && 
        g_old_sigactions[sig].sa_handler != SIG_IGN) {
        g_old_sigactions[sig].sa_sigaction(sig, info, context);
    } else {
        // Re-raise signal with default handler
        signal(sig, SIG_DFL);
        raise(sig);
    }
}
#endif

bool init_crash_handler(const CrashHandlerConfig& config) {
    if (g_crash_handler_initialized) {
        return false;
    }
    
    g_crash_config = config;
    
    // Ensure output directory exists
    try {
        fs::create_directories(g_crash_config.output_directory);
    } catch (...) {
        return false;
    }
    
#ifndef _WIN32
#ifdef __linux__
    // Enable core dumps (Linux only)
    if (g_crash_config.enable_coredump) {
        struct rlimit core_limit;
        core_limit.rlim_cur = RLIM_INFINITY;
        core_limit.rlim_max = RLIM_INFINITY;
        setrlimit(RLIMIT_CORE, &core_limit);
    }
#endif
    
    // Install signal handlers (Unix/POSIX only)
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_signal_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;  // One-shot handler
    sigemptyset(&sa.sa_mask);
    
    int signals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS, SIGTRAP};
    for (int sig : signals) {
        sigaction(sig, &sa, &g_old_sigactions[sig]);
    }
#else
    g_previous_exception_filter = SetUnhandledExceptionFilter(crash_exception_handler);
    g_previous_terminate_handler = std::set_terminate(crash_terminate_handler);
#endif
    
    g_crash_handler_initialized = true;
    
    auto logger = get_logger();
    if (logger) {
#ifndef _WIN32
        logger->debug("Crash handler initialized - bundles will be saved to: {}", 
                     g_crash_config.output_directory);
#else
    logger->debug("Crash handler initialized - bundles will be saved to: {}", 
                     g_crash_config.output_directory);
#endif
    }
    
    return true;
}

std::string get_last_crash_bundle_path() {
    return g_last_crash_bundle;
}

std::string create_crash_bundle(const std::string& error_message) {
    if (!g_crash_handler_initialized) {
        return "";
    }
    
    std::string crash_info = create_crash_info(0, error_message);
    std::string coredump_path = find_coredump();
    
    std::vector<std::string> log_files;
    try {
        for (const auto& entry : fs::directory_iterator(g_crash_config.output_directory)) {
            if (entry.path().extension() == ".log") {
                log_files.push_back(entry.path().string());
            }
        }
    } catch (...) {
        // Continue even if directory listing fails
    }
    
    std::string bundle_path = create_bundle_zip(crash_info, coredump_path, log_files);
    g_last_crash_bundle = bundle_path;
    
    return bundle_path;
}

void cleanup_crash_handler() {
    if (!g_crash_handler_initialized) {
        return;
    }
    
#ifndef _WIN32
    // Restore original signal handlers (Unix/POSIX only)
    int signals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS, SIGTRAP};
    for (int sig : signals) {
        sigaction(sig, &g_old_sigactions[sig], nullptr);
    }
#else
    SetUnhandledExceptionFilter(g_previous_exception_filter);
    std::set_terminate(g_previous_terminate_handler);
    g_previous_exception_filter = nullptr;
    g_previous_terminate_handler = nullptr;
#endif
    
    g_crash_handler_initialized = false;
}

} // namespace orc
