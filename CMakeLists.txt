# Top-level CMakeLists.txt for ld-decode-orc
# This allows building from a top-level build/ directory

cmake_minimum_required(VERSION 3.20)
project(ld-decode-orc-root)

# Add the orc subdirectory which contains the main project
add_subdirectory(orc)
