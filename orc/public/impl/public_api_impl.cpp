// Stub for public_api_impl.cpp
// This file should include both public and core headers, and use type_bridge for conversions.

#include <orc_types.h>
#include "type_bridge.h"
#include "../../core/include/project.h"
#include "../../core/include/logging.h"
#include <string>

// Implement all public API function stubs here, using type_bridge for handle conversion.
// Example:
// ORC_API OrcProjectHandle orc_project_create(const char* path) {
//     auto* project = new orc::Project(path);
//     return orc::public_api::toHandle(project);
// }

// ...existing code...

extern "C" {

void orc_logging_init(const char* level,
					  const char* pattern,
					  const char* log_file)
{
	std::string lvl = level ? std::string(level) : std::string("info");
	std::string pat = pattern ? std::string(pattern) : std::string("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
	std::string file = log_file ? std::string(log_file) : std::string("");
	orc::init_logging(lvl, pat, file);
}

void orc_logging_set_level(const char* level)
{
	std::string lvl = level ? std::string(level) : std::string("info");
	orc::set_log_level(lvl);
}

} // extern "C"
