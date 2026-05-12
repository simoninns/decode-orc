/*
 * File:        plugin_remote_loader.cpp
 * Module:      orc-core
 * Purpose:     Utilities for downloading and caching remote plugin binaries
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "include/plugin_remote_loader.h"

#include <algorithm>
#include <cctype>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <regex>
#include <spdlog/spdlog.h>

namespace orc {
namespace {

static size_t write_string_callback(void* contents, size_t size, size_t nmemb, std::string* output)
{
    size_t real_size = size * nmemb;
    if (!output) {
        return 0;
    }
    output->append(static_cast<const char*>(contents), real_size);
    return real_size;
}

// Callback for libcurl to write downloaded data to file
static size_t write_callback(void* contents, size_t size, size_t nmemb, std::ofstream* file)
{
    size_t real_size = size * nmemb;
    if (!file || !file->is_open()) {
        return 0;
    }
    file->write(static_cast<char*>(contents), real_size);
    return file->good() ? real_size : 0;
}

std::string env_or_empty(const char* name)
{
    const char* value = std::getenv(name);
    if (!value) {
        return "";
    }
    return std::string(value);
}

std::filesystem::path resolve_default_config_dir()
{
#if defined(_WIN32)
    std::string appdata = env_or_empty("APPDATA");
    if (!appdata.empty()) {
        return std::filesystem::path(appdata) / "decode-orc";
    }

    std::string userprofile = env_or_empty("USERPROFILE");
    if (!userprofile.empty()) {
        return std::filesystem::path(userprofile) / "AppData" / "Roaming" / "decode-orc";
    }
#else
    std::string xdg_config_home = env_or_empty("XDG_CONFIG_HOME");
    if (!xdg_config_home.empty()) {
        return std::filesystem::path(xdg_config_home) / "decode-orc";
    }

    std::string home = env_or_empty("HOME");
    if (!home.empty()) {
        return std::filesystem::path(home) / ".config" / "decode-orc";
    }
#endif

    return std::filesystem::current_path() / ".decode-orc";
}

std::string normalize_json_string(std::string value)
{
    std::string::size_type pos = 0;
    while ((pos = value.find("\\/", pos)) != std::string::npos) {
        value.replace(pos, 2, "/");
        ++pos;
    }
    return value;
}

std::string platform_extension(const std::string& target_platform)
{
    if (target_platform == "windows") {
        return ".dll";
    }
    if (target_platform == "macos") {
        return ".dylib";
    }
    return ".so";
}

std::string platform_token(const std::string& target_platform)
{
    if (target_platform == "windows") {
        return "_windows";
    }
    if (target_platform == "macos") {
        return "_macos";
    }
    return "_linux";
}

std::string infer_target_platform()
{
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

bool fetch_text_url(const std::string& url, std::string* body, std::string* error_message)
{
    if (!body) {
        if (error_message) {
            *error_message = "Internal error: body output pointer is null";
        }
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        if (error_message) {
            *error_message = "Failed to initialize libcurl";
        }
        return false;
    }

    body->clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "decode-orc/1.0 (+https://github.com/simoninns/decode-orc)");

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        if (error_message) {
            *error_message = std::string("Failed to fetch URL: ") + curl_easy_strerror(res);
        }
        curl_easy_cleanup(curl);
        return false;
    }

    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);

    if (response_code < 200 || response_code >= 300) {
        if (error_message) {
            *error_message = "HTTP error " + std::to_string(response_code) + " while fetching URL";
        }
        return false;
    }

    return true;
}

std::string sanitize_cache_component(std::string value)
{
    for (char& ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (!std::isalnum(uch) && ch != '.' && ch != '_' && ch != '-') {
            ch = '_';
        }
    }

    if (value.empty()) {
        return "unknown";
    }

    return value;
}

std::string extract_release_tag_from_asset_url(const std::string& github_release_url)
{
    // Expected form: .../releases/download/<tag>/<asset>
    const std::regex download_regex(R"(/releases/download/([^/]+)/)");
    std::smatch match;
    if (!std::regex_search(github_release_url, match, download_regex)) {
        return "";
    }

    return normalize_json_string(match[1].str());
}

} // namespace

PluginRemoteLoader::ResolveReleaseAssetResult PluginRemoteLoader::resolve_release_asset_from_releases_url(
    const std::string& releases_url,
    const std::string& target_platform,
    std::vector<std::string>* warnings)
{
    ResolveReleaseAssetResult result;

    const std::regex repo_regex(R"(^https?://github\.com/([^/]+)/([^/]+)(?:/.*)?$)");
    std::smatch repo_match;
    if (!std::regex_match(releases_url, repo_match, repo_regex)) {
        result.error_message = "Expected a GitHub releases URL like https://github.com/<owner>/<repo>/releases";
        return result;
    }

    const std::string owner = repo_match[1].str();
    std::string repo = repo_match[2].str();
    if (repo.size() > 4 && repo.substr(repo.size() - 4) == ".git") {
        repo = repo.substr(0, repo.size() - 4);
    }

    result.source_repo_url = "https://github.com/" + owner + "/" + repo;

    std::string api_url;
    std::string explicit_tag;

    const std::regex tag_regex(R"(/releases/tag/([^/?#]+))");
    std::smatch tag_match;
    if (std::regex_search(releases_url, tag_match, tag_regex)) {
        explicit_tag = tag_match[1].str();
        api_url = "https://api.github.com/repos/" + owner + "/" + repo + "/releases/tags/" + explicit_tag;
    } else {
        api_url = "https://api.github.com/repos/" + owner + "/" + repo + "/releases/latest";
    }

    std::string response_body;
    std::string fetch_error;
    if (!fetch_text_url(api_url, &response_body, &fetch_error)) {
        result.error_message = "Failed to query GitHub release metadata: " + fetch_error;
        return result;
    }

    {
        const std::regex tag_name_regex(R"json("tag_name"\s*:\s*"([^"]+)")json");
        std::smatch tag_name_match;
        if (std::regex_search(response_body, tag_name_match, tag_name_regex)) {
            result.release_tag = normalize_json_string(tag_name_match[1].str());
        } else if (!explicit_tag.empty()) {
            result.release_tag = explicit_tag;
        }
    }

    struct Candidate {
        std::string url;
        std::string name;
    };
    std::vector<Candidate> candidates;

    const std::regex download_url_regex(R"json("browser_download_url"\s*:\s*"([^"]+)")json");
    for (std::sregex_iterator it(response_body.begin(), response_body.end(), download_url_regex), end;
         it != end; ++it) {
        const std::string raw_url = (*it)[1].str();
        const std::string url = normalize_json_string(raw_url);
        const auto slash = url.find_last_of('/');
        const std::string name = (slash == std::string::npos) ? url : url.substr(slash + 1);
        candidates.push_back({url, name});
    }

    if (candidates.empty()) {
        result.error_message = "No downloadable assets found in the selected GitHub release";
        return result;
    }

    const std::string platform = target_platform.empty() ? infer_target_platform() : target_platform;
    const std::string required_ext = platform_extension(platform);
    const std::string preferred_token = platform_token(platform);

    auto choose_candidate = [&](bool require_token) -> const Candidate* {
        for (const auto& c : candidates) {
            const bool has_prefix = c.name.rfind("orc-plugin_", 0) == 0;
            const bool has_ext = c.name.size() > required_ext.size() &&
                c.name.substr(c.name.size() - required_ext.size()) == required_ext;
            const bool has_token = c.name.find(preferred_token) != std::string::npos;
            if (!has_prefix || !has_ext) {
                continue;
            }
            if (require_token && !has_token) {
                continue;
            }
            return &c;
        }
        return nullptr;
    };

    const Candidate* selected = choose_candidate(true);
    if (!selected) {
        selected = choose_candidate(false);
    }

    if (!selected) {
        result.error_message =
            "No matching plugin asset found for platform '" + platform +
            "' (expected orc-plugin_*" + required_ext + ")";
        return result;
    }

    if (warnings && selected->name.find(preferred_token) == std::string::npos) {
        warnings->push_back(
            "Selected release asset does not include expected platform token '" + preferred_token +
            "'; using best extension match instead");
    }

    result.release_asset_url = selected->url;
    result.release_asset_name = selected->name;
    result.success = true;
    return result;
}

std::string PluginRemoteLoader::resolve_plugin_cache_dir(std::string* error)
{
    try {
        std::filesystem::path cache_dir = resolve_default_config_dir() / "plugin-cache";

        if (!std::filesystem::exists(cache_dir)) {
            std::filesystem::create_directories(cache_dir);
        }

        if (!std::filesystem::is_directory(cache_dir)) {
            if (error) {
                *error = "Plugin cache path exists but is not a directory: " + cache_dir.string();
            }
            return "";
        }

        return cache_dir.string();
    } catch (const std::filesystem::filesystem_error& e) {
        if (error) {
            *error = std::string("Failed to access plugin cache directory: ") + e.what();
        }
        return "";
    }
}

std::string PluginRemoteLoader::get_cache_path(const std::string& asset_name,
                                              const std::string& target_platform,
                                              const std::string& release_tag)
{
    std::string cache_dir_str = resolve_plugin_cache_dir();
    if (cache_dir_str.empty()) {
        return "";
    }

    std::filesystem::path cache_path = std::filesystem::path(cache_dir_str) / target_platform;
    if (!release_tag.empty()) {
        cache_path /= sanitize_cache_component(release_tag);
    }
    cache_path /= asset_name;
    return cache_path.string();
}

PluginRemoteLoader::DownloadResult PluginRemoteLoader::download_release_asset(
    const std::string& github_release_url,
    const std::string& asset_name,
    const std::string& target_platform,
    std::vector<std::string>* warnings)
{
    DownloadResult result;

    // Validate asset name format
    static const std::regex kPattern(
        R"(^orc-plugin_[A-Za-z0-9._-]+_[A-Za-z0-9._-]+\.(so|dylib|dll)$)");
    if (!std::regex_match(asset_name, kPattern)) {
        result.error_message = "Invalid asset name format (expected orc-plugin_<stage>_<platform>.<so|dylib|dll>): " +
                               asset_name;
        if (warnings) {
            warnings->push_back(result.error_message);
        }
        return result;
    }

    const std::string release_tag = extract_release_tag_from_asset_url(github_release_url);

    // Check release-aware cache first. This prevents stale binaries when the same
    // asset name is reused across multiple release tags.
    std::string cache_path = get_cache_path(asset_name, target_platform, release_tag);
    if (!cache_path.empty() && std::filesystem::exists(cache_path)) {
        spdlog::debug("Plugin binary found in cache: {}", cache_path);
        result.success = true;
        result.downloaded_path = cache_path;
        return result;
    }

    // Ensure cache directory exists
    std::string cache_dir_str;
    {
        std::string error_msg;
        cache_dir_str = resolve_plugin_cache_dir(&error_msg);
        if (cache_dir_str.empty()) {
            result.error_message = "Failed to create plugin cache directory: " + error_msg;
            if (warnings) {
                warnings->push_back(result.error_message);
            }
            return result;
        }
    }

    // Create platform-specific subdirectory in cache
    try {
        std::filesystem::path platform_cache = std::filesystem::path(cache_dir_str) / target_platform;
        if (!release_tag.empty()) {
            platform_cache /= sanitize_cache_component(release_tag);
        }
        if (!std::filesystem::exists(platform_cache)) {
            std::filesystem::create_directories(platform_cache);
        }
        cache_path = (platform_cache / asset_name).string();
    } catch (const std::filesystem::filesystem_error& e) {
        result.error_message =
            std::string("Failed to create platform cache subdirectory: ") + e.what();
        if (warnings) {
            warnings->push_back(result.error_message);
        }
        return result;
    }

    // Initialize libcurl
    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error_message = "Failed to initialize libcurl";
        if (warnings) {
            warnings->push_back(result.error_message);
        }
        return result;
    }

    // Open output file
    std::ofstream outfile(cache_path, std::ios::binary);
    if (!outfile.is_open()) {
        result.error_message = "Failed to open cache file for writing: " + cache_path;
        if (warnings) {
            warnings->push_back(result.error_message);
        }
        curl_easy_cleanup(curl);
        return result;
    }

    spdlog::debug("Downloading plugin binary from: {}", github_release_url);

    // Configure libcurl
    curl_easy_setopt(curl, CURLOPT_URL, github_release_url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outfile);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);  // Follow redirects (GitHub redirects)
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);       // 5 minute timeout
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);     // Fail on HTTP errors

    // Add User-Agent to be respectful
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "decode-orc/1.0 (+https://github.com/simoninns/decode-orc)");

    // Perform download
    CURLcode res = curl_easy_perform(curl);

    outfile.close();

    if (res != CURLE_OK) {
        std::string error = std::string("Failed to download plugin binary: ") + curl_easy_strerror(res);
        result.error_message = error;
        if (warnings) {
            warnings->push_back(error);
        }

        // Clean up failed download
        try {
            std::filesystem::remove(cache_path);
        } catch (...) {
        }

        curl_easy_cleanup(curl);
        return result;
    }

    // Check response code
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    curl_easy_cleanup(curl);

    if (response_code < 200 || response_code >= 300) {
        std::string error = std::string("HTTP error ") + std::to_string(response_code) +
                           " downloading plugin binary from: " + github_release_url;
        result.error_message = error;
        if (warnings) {
            warnings->push_back(error);
        }

        // Clean up failed download
        try {
            std::filesystem::remove(cache_path);
        } catch (...) {
        }

        return result;
    }

    // Verify file was written
    if (!std::filesystem::exists(cache_path)) {
        std::string error = "Downloaded file not found at cache path: " + cache_path;
        result.error_message = error;
        if (warnings) {
            warnings->push_back(error);
        }
        return result;
    }

    uint64_t file_size = std::filesystem::file_size(cache_path);
    if (file_size == 0) {
        std::string error = "Downloaded file is empty (0 bytes): " + cache_path;
        result.error_message = error;
        if (warnings) {
            warnings->push_back(error);
        }

        // Clean up empty file
        try {
            std::filesystem::remove(cache_path);
        } catch (...) {
        }

        return result;
    }

    spdlog::debug("Successfully downloaded plugin binary to cache: {} ({} bytes)", cache_path, file_size);

    result.success = true;
    result.downloaded_path = cache_path;
    return result;
}

} // namespace orc
