# External stage-plugin fixture for validating the installed
# decode-orc-plugin-sdk CMake package.
#
# This project is NOT part of the in-tree build: it is configured standalone
# against an installed SDK prefix, exactly as a third-party plugin author
# would build (CI: .github/workflows/build-and-test.yml, installed-package
# validation step):
#
#   cmake --install <host-build-dir> --prefix <scratch-prefix>
#   cmake -S orc-tests/fixtures/external-stage-plugin -B <fixture-build-dir> \
#         -DCMAKE_PREFIX_PATH=<scratch-prefix>
#   cmake --build <fixture-build-dir>
#
# Living under orc-tests/ keeps it outside the in-tree plugin scan root
# (orc/plugins/stages); the installed gate scripts are run against this
# directory explicitly in standalone-tree mode.

cmake_minimum_required(VERSION 3.20)
project(orc-external-fixture-plugin VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(decode-orc-plugin-sdk REQUIRED)

orc_add_stage_plugin(
    orc-stage-plugin-external-fixture
    OUTPUT_NAME    orc-stage-plugin-external-fixture
    PLUGIN_VERSION "${PROJECT_VERSION}"
    SOURCES        plugin.cpp
)
