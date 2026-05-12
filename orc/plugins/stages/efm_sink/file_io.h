/*
 * File:        file_io.h
 * Purpose:     No description
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef FILE_IO_H
#define FILE_IO_H

#include <string>
#include <vector>
#include <cstring>
#include <fstream>
#include <iostream>
#include <cstdint>

// Write binary data to a file
inline bool writeBinaryData(const std::string &filename, const std::vector<uint8_t> &data) {
    if (filename == "-") {
        // Write to stdout
        std::cout.write(reinterpret_cast<const char*>(data.data()), data.size());
        return std::cout.good();
    } else {
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        bool result = file.good();
        file.close();
        return result;
    }
}

// Read binary data from a file
inline std::vector<uint8_t> readBinaryData(const std::string &filename, size_t maxSize = 0) {
    std::vector<uint8_t> result;
    
    if (filename == "-") {
        // Read from stdin
        std::vector<uint8_t> buffer(4096);
        while (std::cin.read(reinterpret_cast<char*>(buffer.data()), buffer.size()) || std::cin.gcount() > 0) {
            result.insert(result.end(), buffer.begin(), buffer.begin() + std::cin.gcount());
            if (maxSize > 0 && result.size() >= maxSize) {
                result.resize(maxSize);
                break;
            }
        }
    } else {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            return result;
        }
        
        std::vector<uint8_t> buffer(4096);
        while (file.read(reinterpret_cast<char*>(buffer.data()), buffer.size()) || file.gcount() > 0) {
            result.insert(result.end(), buffer.begin(), buffer.begin() + file.gcount());
            if (maxSize > 0 && result.size() >= maxSize) {
                result.resize(maxSize);
                break;
            }
        }
        file.close();
    }
    
    return result;
}

// Write a single byte
inline bool writeByte(std::ofstream &file, uint8_t byte) {
    file.write(reinterpret_cast<const char*>(&byte), 1);
    return file.good();
}

// Write a 32-bit integer (little-endian)
inline bool writeUint32LE(std::ofstream &file, uint32_t value) {
    uint8_t bytes[4] = {
        static_cast<uint8_t>((value >> 0) & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF),
        static_cast<uint8_t>((value >> 16) & 0xFF),
        static_cast<uint8_t>((value >> 24) & 0xFF)
    };
    file.write(reinterpret_cast<const char*>(bytes), 4);
    return file.good();
}

// Read a 32-bit integer (little-endian)
inline uint32_t readUint32LE(std::ifstream &file) {
    uint8_t bytes[4] = {0, 0, 0, 0};
    file.read(reinterpret_cast<char*>(bytes), 4);
    return (static_cast<uint32_t>(bytes[0]) << 0) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

// Get file size
inline int64_t getFileSize(const std::string &filename) {
    if (filename == "-") {
        // Can't determine stdin size
        return -1;
    }
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return -1;
    }
    return file.tellg();
}

#endif // FILE_IO_H
