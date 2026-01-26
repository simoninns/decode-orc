#ifndef ORC_PUBLIC_LOGGING_H
#define ORC_PUBLIC_LOGGING_H

#ifdef __cplusplus
extern "C" {
#endif

// Initialize core logging via the public API bridge
// level: "trace", "debug", "info", "warn", "error", "critical", "off"
// pattern: spdlog pattern string or NULL for default
// log_file: optional file path or NULL for console only
void orc_logging_init(const char* level,
                      const char* pattern,
                      const char* log_file);

// Set core log level at runtime
void orc_logging_set_level(const char* level);

#ifdef __cplusplus
}
#endif

#endif // ORC_PUBLIC_LOGGING_H
