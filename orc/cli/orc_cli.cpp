/*
 * File:        orc_cli.cpp
 * Module:      orc-cli
 * Purpose:     CLI application with subcommands
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "version.h"
#include "command_process.h"
#include "logging.h"
#include "crash_handler.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

using namespace orc;

/**
 * @brief Print command-line usage information
 * 
 * Displays help text showing available commands, options, and examples
 * for the orc-cli command-line tool.
 * 
 * @param program_name Name of the executable (argv[0])
 */
void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " <project-file> [options]\n";
    std::cerr << "\n";
    std::cerr << "Commands:\n";
    std::cerr << "  --process                      Process the whole DAG chain (trigger all sinks)\n";
    std::cerr << "\n";
    std::cerr << "Options:\n";
    std::cerr << "  --log-level LEVEL              Set logging verbosity\n";
    std::cerr << "                                 (trace, debug, info, warn, error, critical, off)\n";
    std::cerr << "                                 Default: info\n";
    std::cerr << "  --log-file FILE                Write logs to specified file\n";
    std::cerr << "\n";
    std::cerr << "Examples:\n";
    std::cerr << "  " << program_name << " project.orcprj --process\n";
    std::cerr << "  " << program_name << " project.orcprj --process --log-level debug\n";
}

/**
 * @brief Main entry point for orc-cli
 * 
 * Parses command-line arguments and dispatches to the appropriate command handler.
 * Supports processing projects, analyzing field mappings, and analyzing source alignments.
 * 
 * @param argc Argument count
 * @param argv Argument values
 * @return Exit code (0 = success, non-zero = error)
 */
int main(int argc, char* argv[]) {
    // Parse command line arguments
    std::string project_path;
    std::string log_level = "info";
    std::string log_file;
    
    // Command flags
    bool do_process = false;
    
    // Check for help or empty args
    if (argc < 2) {
        std::cerr << "Error: No project file or command specified\n\n";
        print_usage(argv[0]);
        return 1;
    }
    
    std::string first_arg = argv[1];
    if (first_arg == "--help" || first_arg == "-h") {
        print_usage(argv[0]);
        return 0;
    }
    
    // Parse all arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--log-level" && i + 1 < argc) {
            log_level = argv[++i];
        } else if (arg == "--log-file" && i + 1 < argc) {
            log_file = argv[++i];
        } else if (arg == "--process") {
            do_process = true;
        } else if (arg[0] != '-') {
            // Positional argument - project file
            if (project_path.empty()) {
                project_path = arg;
            } else {
                std::cerr << "Error: Multiple project files specified\n";
                print_usage(argv[0]);
                return 1;
            }
        } else {
            std::cerr << "Error: Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Check if project file was provided
    if (project_path.empty()) {
        std::cerr << "Error: No project file specified\n\n";
        print_usage(argv[0]);
        return 1;
    }
    
    // Check if at least one command was specified
    if (!do_process) {
        std::cerr << "Error: No command specified. You must use --process\n\n";
        print_usage(argv[0]);
        return 1;
    }
    
    // Initialize logging
    orc::init_app_logging(log_level, "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v", log_file);
    
    // Initialize crash handler
    CrashHandlerConfig crash_config;
    crash_config.application_name = "orc-cli";
    crash_config.version = ORC_VERSION;
    crash_config.output_directory = fs::current_path().string();
    crash_config.enable_coredump = true;
    crash_config.auto_upload_info = true;
    
    // Add callback for custom application state
    crash_config.custom_info_callback = [&project_path]() -> std::string {
        std::ostringstream info;
        info << "Project file: " << project_path << "\n";
        info << "Working directory: " << fs::current_path().string() << "\n";
        return info.str();
    };
    
    if (!init_crash_handler(crash_config)) {
        ORC_LOG_WARN("Failed to initialize crash handler");
    }
    
    // Execute processing command
    int exit_code = 0;
    
    try {
        cli::ProcessOptions options;
        options.project_path = project_path;
        
        exit_code = cli::process_command(options);
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL ERROR: " << e.what() << "\n";
        
        // Create crash bundle for unhandled exceptions
        std::string bundle_path = create_crash_bundle(std::string("Exception: ") + e.what());
        if (!bundle_path.empty()) {
            std::cerr << "\nDiagnostic bundle created: " << bundle_path << "\n";
            std::cerr << "Please report this issue at: https://github.com/simoninns/decode-orc/issues\n";
        }
        
        cleanup_crash_handler();
        return 1;
    } catch (...) {
        std::cerr << "\nFATAL ERROR: Unknown exception occurred\n";
        
        // Create crash bundle for unknown exceptions
        std::string bundle_path = create_crash_bundle("Unknown exception");
        if (!bundle_path.empty()) {
            std::cerr << "\nDiagnostic bundle created: " << bundle_path << "\n";
            std::cerr << "Please report this issue at: https://github.com/simoninns/decode-orc/issues\n";
        }
        
        cleanup_crash_handler();
        return 1;
    }
    
    cleanup_crash_handler();
    return exit_code;
}
