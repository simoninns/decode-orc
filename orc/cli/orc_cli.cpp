/*
 * File:        orc_cli.cpp
 * Module:      orc-cli
 * Purpose:     CLI application with subcommands
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "version.h"
#include "command_process.h"
#include "logging.h"

#include <iostream>
#include <string>
#include <vector>

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
    orc::init_logging(log_level, "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v", log_file);
    
    // Execute processing command
    int exit_code = 0;
    
    try {
        cli::ProcessOptions options;
        options.project_path = project_path;
        
        exit_code = cli::process_command(options);
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL ERROR: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "\nFATAL ERROR: Unknown exception occurred\n";
        return 1;
    }
    
    return exit_code;
}
