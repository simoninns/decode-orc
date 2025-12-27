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
#include "command_analyze_field_mapping.h"
#include "logging.h"

#include <iostream>
#include <string>

using namespace orc;

void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " <command> [options]\n";
    std::cerr << "\n";
    std::cerr << "Commands:\n";
    std::cerr << "  process                        Process project DAG and trigger sinks\n";
    std::cerr << "  analyze-field-mapping          Analyze field mapping for a project\n";
    std::cerr << "\n";
    std::cerr << "Global Options:\n";
    std::cerr << "  --log-level LEVEL              Set logging verbosity\n";
    std::cerr << "                                 (trace, debug, info, warn, error, critical, off)\n";
    std::cerr << "                                 Default: info\n";
    std::cerr << "  --log-file FILE                Write logs to specified file\n";
    std::cerr << "\n";
    std::cerr << "Run '" << program_name << " <command> --help' for command-specific help.\n";
}

void print_process_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " process <project-file> [options]\n";
    std::cerr << "\n";
    std::cerr << "Process an ORC project by executing the DAG and triggering all sink nodes.\n";
    std::cerr << "\n";
    std::cerr << "Arguments:\n";
    std::cerr << "  project-file                   Path to ORC project file (.orcprj)\n";
    std::cerr << "\n";
    std::cerr << "Example:\n";
    std::cerr << "  " << program_name << " process project.orcprj\n";
    std::cerr << "  " << program_name << " process project.orcprj --log-level debug\n";
}

void print_analyze_field_mapping_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " analyze-field-mapping <project-file> [options]\n";
    std::cerr << "\n";
    std::cerr << "Analyze field mapping for a TBC source and optionally update the project.\n";
    std::cerr << "\n";
    std::cerr << "Arguments:\n";
    std::cerr << "  project-file                   Path to ORC project file (.orcprj)\n";
    std::cerr << "\n";
    std::cerr << "Options:\n";
    std::cerr << "  --update-project               Update project file with mapping spec\n";
    std::cerr << "  --no-pad-gaps                  Don't pad gaps with black frames\n";
    std::cerr << "  --delete-unmappable            Delete frames that can't be mapped\n";
    std::cerr << "\n";
    std::cerr << "Examples:\n";
    std::cerr << "  # Analyze only (no project update):\n";
    std::cerr << "  " << program_name << " analyze-field-mapping project.orcprj\n";
    std::cerr << "\n";
    std::cerr << "  # Analyze and update project:\n";
    std::cerr << "  " << program_name << " analyze-field-mapping project.orcprj --update-project\n";
    std::cerr << "\n";
    std::cerr << "  # Then process the updated project:\n";
    std::cerr << "  " << program_name << " process project.orcprj\n";
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    std::string command;
    std::string project_path;
    std::string log_level = "info";
    std::string log_file;
    
    // Command-specific options
    bool update_project = false;
    bool pad_gaps = true;
    bool delete_unmappable = false;
    
    // First pass: extract command
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string first_arg = argv[1];
    if (first_arg == "--help" || first_arg == "-h") {
        print_usage(argv[0]);
        return 0;
    }
    
    // Check if first argument is a command
    if (first_arg == "process" || first_arg == "analyze-field-mapping") {
        command = first_arg;
    } else {
        // Backwards compatibility: if no command, assume "process"
        command = "process";
        project_path = first_arg;
    }
    
    // Parse remaining arguments
    int start_idx = (command == first_arg) ? 2 : 1;
    for (int i = start_idx; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            if (command == "process") {
                print_process_usage(argv[0]);
            } else if (command == "analyze-field-mapping") {
                print_analyze_field_mapping_usage(argv[0]);
            }
            return 0;
        } else if (arg == "--log-level" && i + 1 < argc) {
            log_level = argv[++i];
        } else if (arg == "--log-file" && i + 1 < argc) {
            log_file = argv[++i];
        } else if (arg == "--update-project") {
            update_project = true;
        } else if (arg == "--no-pad-gaps") {
            pad_gaps = false;
        } else if (arg == "--delete-unmappable") {
            delete_unmappable = true;
        } else if (arg[0] != '-') {
            // Positional argument - project file
            if (project_path.empty()) {
                project_path = arg;
            } else {
                std::cerr << "Error: Multiple project files specified\n";
                if (command == "process") {
                    print_process_usage(argv[0]);
                } else if (command == "analyze-field-mapping") {
                    print_analyze_field_mapping_usage(argv[0]);
                } else {
                    print_usage(argv[0]);
                }
                return 1;
            }
        } else {
            std::cerr << "Error: Unknown option: " << arg << "\n";
            if (command == "process") {
                print_process_usage(argv[0]);
            } else if (command == "analyze-field-mapping") {
                print_analyze_field_mapping_usage(argv[0]);
            } else {
                print_usage(argv[0]);
            }
            return 1;
        }
    }
    
    // Check if project file was provided
    if (project_path.empty()) {
        if (command == "process") {
            print_process_usage(argv[0]);
        } else if (command == "analyze-field-mapping") {
            print_analyze_field_mapping_usage(argv[0]);
        } else {
            print_usage(argv[0]);
        }
        return 1;
    }
    
    // Initialize logging
    orc::init_logging(log_level, "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v", log_file);
    
    // Dispatch to command with exception handling
    try {
        if (command == "process") {
            cli::ProcessOptions options;
            options.project_path = project_path;
            return cli::process_command(options);
        } else if (command == "analyze-field-mapping") {
            cli::AnalyzeFieldMappingOptions options;
            options.project_path = project_path;
            options.update_project = update_project;
            options.pad_gaps = pad_gaps;
            options.delete_unmappable = delete_unmappable;
            return cli::analyze_field_mapping_command(options);
        } else {
            std::cerr << "Error: Unknown command: " << command << "\n";
            print_usage(argv[0]);
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL ERROR: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "\nFATAL ERROR: Unknown exception occurred\n";
        return 1;
    }
}
