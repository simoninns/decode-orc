/*
 * File:        frame.cpp
 * Module:      EFM-library
 * Purpose:     EFM Frame type classes
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "frame.h"
#include "core/logging.h"
#include <cstdlib>
#include <cstdio>
#include "hex_utils.h"

// Frame class
// --------------------------------------------------------------------------------------------------------

// Set the data for the frame, ensuring it matches the frame size
void Frame::setData(const std::vector<uint8_t> &data)
{
    if (static_cast<int>(data.size()) != frameSize()) {
        LOG_ERROR("Frame::setData(): Data size of {} does not match frame size of {}", data.size(), frameSize());
        std::exit(1);
    }
    m_frameData = data;
}

// Get the data for the frame, returning a zero-filled vector if empty
std::vector<uint8_t> Frame::data() const
{
    if (m_frameData.empty()) {
        LOG_DEBUG("Frame::getData(): Frame is empty, returning zero-filled vector");
        return std::vector<uint8_t>(frameSize(), 0);
    }
    return m_frameData;
}

const std::vector<uint8_t> &Frame::dataRef() const
{
    if (m_frameData.empty()) {
        if (static_cast<int>(m_defaultFrameData.size()) != frameSize()) {
            m_defaultFrameData.assign(frameSize(), 0);
        }
        return m_defaultFrameData;
    }
    return m_frameData;
}

// Set the error data for the frame, ensuring it matches the frame size
// Note: This is a vector of boolean, where false is no error and true is an error
void Frame::setErrorData(const std::vector<bool> &errorData)
{
    if (static_cast<int>(errorData.size()) != frameSize()) {
        LOG_ERROR("Frame::setErrorData(): Error data size of {} does not match frame size of {}", errorData.size(), frameSize());
        std::exit(1);
    }

    m_frameErrorData.resize(errorData.size());
    for (size_t i = 0; i < errorData.size(); ++i) {
        m_frameErrorData[i] = errorData[i] ? 1 : 0;
    }
}

// Get the error_data for the frame, returning a zero-filled vector if empty
// Note: This is a vector of boolean, where false is no error and true is an error
std::vector<bool> Frame::errorData() const
{
    if (m_frameErrorData.empty()) {
        LOG_DEBUG("Frame::getErrorData(): Error frame is empty, returning zero-filled vector");
        return std::vector<bool>(frameSize(), 0);
    }

    std::vector<bool> out;
    out.resize(m_frameErrorData.size());
    for (size_t i = 0; i < m_frameErrorData.size(); ++i) {
        out[i] = m_frameErrorData[i] != 0;
    }
    return out;
}

const std::vector<bool> &Frame::errorDataRef() const
{
    if (m_frameErrorData.empty()) {
        if (static_cast<int>(m_defaultFrameErrorDataBoolCache.size()) != frameSize()) {
            m_defaultFrameErrorDataBoolCache.assign(frameSize(), false);
        }
        return m_defaultFrameErrorDataBoolCache;
    }

    m_frameErrorDataBoolCache.resize(m_frameErrorData.size());
    for (size_t i = 0; i < m_frameErrorData.size(); ++i) {
        m_frameErrorDataBoolCache[i] = m_frameErrorData[i] != 0;
    }
    return m_frameErrorDataBoolCache;
}

void Frame::setErrorDataBytes(const std::vector<uint8_t> &errorData)
{
    if (static_cast<int>(errorData.size()) != frameSize()) {
        LOG_ERROR("Frame::setErrorDataBytes(): Error data size of {} does not match frame size of {}", errorData.size(), frameSize());
        std::exit(1);
    }

    m_frameErrorData = errorData;
}

std::vector<uint8_t> Frame::errorDataBytes() const
{
    if (m_frameErrorData.empty()) {
        LOG_DEBUG("Frame::errorDataBytes(): Error frame is empty, returning zero-filled vector");
        return std::vector<uint8_t>(frameSize(), 0);
    }
    return m_frameErrorData;
}

const std::vector<uint8_t> &Frame::errorDataBytesRef() const
{
    if (m_frameErrorData.empty()) {
        if (static_cast<int>(m_defaultFrameErrorData.size()) != frameSize()) {
            m_defaultFrameErrorData.assign(frameSize(), 0);
        }
        return m_defaultFrameErrorData;
    }
    return m_frameErrorData;
}

// Count the number of errors in the frame
uint32_t Frame::countErrors() const
{
    uint32_t errorCount = 0;
    for (int i = 0; i < m_frameErrorData.size(); ++i) {
        if (m_frameErrorData[i] != 0) {
            errorCount++;
        }
    }
    return errorCount;
}

// Set the padded data for the frame, ensuring it matches the frame size
// Note: This is a vector of boolean, where false is no padding and true is padding
void Frame::setPaddedData(const std::vector<bool> &paddedData)
{
    if (paddedData.size() != frameSize()) {
        LOG_ERROR("Frame::setPaddedData(): Padded data size of {} does not match frame size of {}", static_cast<int>(paddedData.size()), frameSize());
        std::exit(1);
    }

    m_framePaddedData.resize(paddedData.size());
    for (size_t i = 0; i < paddedData.size(); ++i) {
        m_framePaddedData[i] = paddedData[i] ? 1 : 0;
    }
}

// Get the padded data for the frame, returning a zero-filled vector if empty
// Note: This is a vector of boolean, where false is no padding and true is padding
std::vector<bool> Frame::paddedData() const
{
    if (m_framePaddedData.empty()) {
        LOG_DEBUG("Frame::paddedData(): Padded data is empty, returning zero-filled vector");
        return std::vector<bool>(frameSize(), 0);
    }

    std::vector<bool> out;
    out.resize(m_framePaddedData.size());
    for (size_t i = 0; i < m_framePaddedData.size(); ++i) {
        out[i] = m_framePaddedData[i] != 0;
    }
    return out;
}

const std::vector<bool> &Frame::paddedDataRef() const
{
    if (m_framePaddedData.empty()) {
        if (static_cast<int>(m_defaultFramePaddedDataBoolCache.size()) != frameSize()) {
            m_defaultFramePaddedDataBoolCache.assign(frameSize(), false);
        }
        return m_defaultFramePaddedDataBoolCache;
    }

    m_framePaddedDataBoolCache.resize(m_framePaddedData.size());
    for (size_t i = 0; i < m_framePaddedData.size(); ++i) {
        m_framePaddedDataBoolCache[i] = m_framePaddedData[i] != 0;
    }
    return m_framePaddedDataBoolCache;
}

void Frame::setPaddedDataBytes(const std::vector<uint8_t> &paddedData)
{
    if (static_cast<int>(paddedData.size()) != frameSize()) {
        LOG_ERROR("Frame::setPaddedDataBytes(): Padded data size of {} does not match frame size of {}", static_cast<int>(paddedData.size()), frameSize());
        std::exit(1);
    }

    m_framePaddedData = paddedData;
}

std::vector<uint8_t> Frame::paddedDataBytes() const
{
    if (m_framePaddedData.empty()) {
        LOG_DEBUG("Frame::paddedDataBytes(): Padded data is empty, returning zero-filled vector");
        return std::vector<uint8_t>(frameSize(), 0);
    }
    return m_framePaddedData;
}

const std::vector<uint8_t> &Frame::paddedDataBytesRef() const
{
    if (m_framePaddedData.empty()) {
        if (static_cast<int>(m_defaultFramePaddedData.size()) != frameSize()) {
            m_defaultFramePaddedData.assign(frameSize(), 0);
        }
        return m_defaultFramePaddedData;
    }
    return m_framePaddedData;
}

// Count the number of padded bytes in the frame
uint32_t Frame::countPadded() const
{
    uint32_t paddingCount = 0;
    for (int i = 0; i < m_framePaddedData.size(); ++i) {
        if (m_framePaddedData[i] != 0) {
            paddingCount++;
        }
    }
    return paddingCount;
}

// Check if the frame is full (i.e., has data)
bool Frame::isFull() const
{
    return !m_frameData.empty();
}

// Check if the frame is empty (i.e., has no data)
bool Frame::isEmpty() const
{
    return m_frameData.empty();
}

// NOTE: QDataStream operators disabled for C++17 migration
// Serialization of Frame objects is not currently supported
/*
QDataStream& operator<<(QDataStream& out, const Frame& frame)
{
    // Write frame data
    out << frame.m_frameData;
    // Write error data
    out << frame.m_frameErrorData;
    // Write padding data
    out << frame.m_framePaddedData;
    return out;
}

QDataStream& operator>>(QDataStream& in, Frame& frame)
{
    // Read frame data
    in >> frame.m_frameData;
    // Read error data
    in >> frame.m_frameErrorData;
    // Read padded data
    in >> frame.m_framePaddedData;
    return in;
}
*/

// Constructor for Data24, initializes data to the frame size
Data24::Data24()
{
    m_frameData.resize(frameSize());
    m_frameErrorData.resize(frameSize());
    std::fill(m_frameErrorData.begin(), m_frameErrorData.end(), false);
    m_framePaddedData.resize(frameSize());
    std::fill(m_framePaddedData.begin(), m_framePaddedData.end(), false);
}

// We override the set_data function to ensure the data is 24 bytes
// since it's possible to have less than 24 bytes of data
void Data24::setData(const std::vector<uint8_t> &data)
{
    m_frameData = data;

    // If there are less than 24 bytes, pad data with zeros to 24 bytes
    if (m_frameData.size() < 24) {
        m_frameData.resize(24);
        for (int i = m_frameData.size(); i < 24; ++i) {
            m_frameData[i] = 0;
        }
    }
}

void Data24::setErrorData(const std::vector<bool> &errorData)
{
    m_frameErrorData.resize(errorData.size());
    for (size_t i = 0; i < errorData.size(); ++i) {
        m_frameErrorData[i] = errorData[i] ? 1 : 0;
    }

    // If there are less than 24 values, pad data with false to 24 values
    if (m_frameErrorData.size() < 24) {
        m_frameErrorData.resize(24);
        for (int i = m_frameErrorData.size(); i < 24; ++i) {
            m_frameErrorData[i] = 0;
        }
    }
}

// Get the frame size for Data24
int Data24::frameSize() const
{
    return 24;
}

void Data24::showData()
{
    if (!get_logger()->should_log(spdlog::level::trace)) return;

    std::string dataString;
    bool hasError = false;
    char buffer[4];
    for (int i = 0; i < m_frameData.size(); ++i) {
        if (m_frameErrorData[i] == 0 && m_framePaddedData[i] == 0) {
            snprintf(buffer, sizeof(buffer), "%02X ", m_frameData[i]);
            dataString.append(buffer);
        } else {
            if (m_framePaddedData[i] != 0) {
                dataString.append("PP ");
            } else {
                dataString.append("XX ");
                hasError = true;
            }
        }
    }
    if (hasError) {
        LOG_TRACE("Data24: {} ERROR", HexUtils::trim(dataString));
    } else {
        LOG_TRACE("Data24: {}", HexUtils::trim(dataString));
    }
}

// Constructor for F1Frame, initializes data to the frame size
F1Frame::F1Frame()
{
    m_frameData.resize(frameSize());
    m_frameErrorData.resize(frameSize());
    std::fill(m_frameErrorData.begin(), m_frameErrorData.end(), false);
    m_framePaddedData.resize(frameSize());
    std::fill(m_framePaddedData.begin(), m_framePaddedData.end(), false);
}

// Get the frame size for F1Frame
int F1Frame::frameSize() const
{
    return 24;
}

void F1Frame::showData()
{
    if (!get_logger()->should_log(spdlog::level::trace)) return;

    std::string dataString;
    bool hasError = false;
    char buffer[4];
    for (int i = 0; i < m_frameData.size(); ++i) {
        if (m_frameErrorData[i] == 0 && m_framePaddedData[i] == 0) {
            snprintf(buffer, sizeof(buffer), "%02X ", m_frameData[i]);
            dataString.append(buffer);
        } else {
            if (m_framePaddedData[i] != 0) {
                dataString.append("PP ");
            } else {
                dataString.append("XX ");
                hasError = true;
            }
        }
    }
    if (hasError) {
        LOG_TRACE("F1Frame: {} ERROR", HexUtils::trim(dataString));
    } else {
        LOG_TRACE("F1Frame: {}", HexUtils::trim(dataString));
    }
}

// Constructor for F2Frame, initializes data to the frame size
F2Frame::F2Frame()
{
    m_frameData.resize(frameSize());
    m_frameErrorData.resize(frameSize());
    std::fill(m_frameErrorData.begin(), m_frameErrorData.end(), false);
    m_framePaddedData.resize(frameSize());
    std::fill(m_framePaddedData.begin(), m_framePaddedData.end(), false);
}

// Get the frame size for F2Frame
int F2Frame::frameSize() const
{
    return 32;
}

void F2Frame::showData()
{
    std::string dataString;
    bool hasError = false;
    char buffer[4];
    for (int i = 0; i < m_frameData.size(); ++i) {
        if (m_frameErrorData[i] == 0 && m_framePaddedData[i] == 0) {
            snprintf(buffer, sizeof(buffer), "%02X ", m_frameData[i]);
            dataString.append(buffer);
        } else {
            if (m_framePaddedData[i] != 0) {
                dataString.append("PP ");
            } else {
                dataString.append("XX ");
                hasError = true;
            }
        }
    }
    if (hasError) {
        LOG_INFO("F2Frame: {} ERROR", HexUtils::trim(dataString));
    } else {
        LOG_INFO("F2Frame: {}", HexUtils::trim(dataString));
    }
}

// Constructor for F3Frame, initializes data to the frame size
F3Frame::F3Frame()
{
    m_frameData.resize(frameSize());
    m_subcodeByte = 0;
    m_f3FrameType = Subcode;
}

// Get the frame size for F3Frame
int F3Frame::frameSize() const
{
    return 32;
}

// Set the frame type as subcode and set the subcode value
void F3Frame::setFrameTypeAsSubcode(uint8_t subcodeValue)
{
    m_f3FrameType = Subcode;
    m_subcodeByte = subcodeValue;
}

// Set the frame type as sync0 and set the subcode value to 0
void F3Frame::setFrameTypeAsSync0()
{
    m_f3FrameType = Sync0;
    m_subcodeByte = 0;
}

// Set the frame type as sync1 and set the subcode value to 0
void F3Frame::setFrameTypeAsSync1()
{
    m_f3FrameType = Sync1;
    m_subcodeByte = 0;
}

// Get the F3 frame type
F3Frame::F3FrameType F3Frame::f3FrameType() const
{
    return m_f3FrameType;
}

// Get the F3 frame type as a string
std::string F3Frame::f3FrameTypeAsString() const
{
    switch (m_f3FrameType) {
    case Subcode:
        return "Subcode";
    case Sync0:
        return "Sync0";
    case Sync1:
        return "Sync1";
    default:
        return "UNKNOWN";
    }
}

// Get the subcode value
uint8_t F3Frame::subcodeByte() const
{
    return m_subcodeByte;
}

void F3Frame::showData()
{
    std::string dataString;
    bool hasError = false;
    for (size_t i = 0; i < m_frameData.size(); ++i) {
        if (m_frameErrorData[i] == 0) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02x ", m_frameData[i]);
            dataString += buf;
        } else {
            dataString += "XX ";
            hasError = true;
        }
    }

    std::string errorString = hasError ? "ERROR" : "";

    char subcodeStr[16];
    snprintf(subcodeStr, sizeof(subcodeStr), "0x%02x", m_subcodeByte);

    if (m_f3FrameType == Subcode) {
        LOG_INFO("F3Frame: {} subcode: {} {}", dataString, subcodeStr, errorString);
    } else if (m_f3FrameType == Sync0) {
        LOG_INFO("F3Frame: {} Sync0 {}", dataString, errorString);
    } else if (m_f3FrameType == Sync1) {
        LOG_INFO("F3Frame: {} Sync1 {}", dataString, errorString);
    } else {
        LOG_INFO("F3Frame: {} UNKNOWN {}", dataString, errorString);
    }
}
