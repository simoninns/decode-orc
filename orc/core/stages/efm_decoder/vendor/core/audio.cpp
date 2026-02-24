/*
 * File:        audio.cpp
 * Module:      EFM-library
 * Purpose:     Audio frame type class
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "audio.h"
#include "logging.h"
#include <cstdlib>
#include <cmath>
#include <cstdio>

// Audio class
// Set the data for the audio, ensuring it matches the frame size
void Audio::setData(const std::vector<int16_t> &data)
{
    if (data.size() != static_cast<size_t>(frameSize())) {
        LOG_ERROR("Audio::setData(): Data size of {} does not match frame size of {}", data.size(), frameSize());
        std::exit(1);
    }
    m_audioData = data;
}

// Set the left and right channel data for the audio, ensuring they match the frame size
void Audio::setDataLeftRight(const std::vector<int16_t> &dataLeft, const std::vector<int16_t> &dataRight)
{
    if (dataLeft.size() + dataRight.size() != static_cast<size_t>(frameSize())) {
        LOG_ERROR("Audio::setDataLeftRight(): Data size of {} does not match frame size of {}",
                     dataLeft.size() + dataRight.size(), frameSize());
        std::exit(1);
    }
    
    m_audioData.clear();
    for (int i = 0; i < frameSize(); i += 2) {
        m_audioData.push_back(dataLeft[i]);
        m_audioData.push_back(dataRight[i]);
    }
}

// Get the data for the audio, returning a zero-filled vector if empty
std::vector<int16_t> Audio::data() const
{
    if (m_audioData.empty()) {
        LOG_DEBUG("Audio::data(): Frame is empty, returning zero-filled vector");
        return std::vector<int16_t>(frameSize(), 0);
    }
    return m_audioData;
}

// Get the left channel data for the audio, returning a zero-filled vector if empty
std::vector<int16_t> Audio::dataLeft() const
{
    if (m_audioData.empty()) {
        LOG_DEBUG("Audio::dataLeft(): Frame is empty, returning zero-filled vector");
        return std::vector<int16_t>(frameSize(), 0);
    }
    
    std::vector<int16_t> dataLeft;
    for (int i = 0; i < frameSize(); i += 2) {
        dataLeft.push_back(m_audioData[i]);
    }
    return dataLeft;
}

// Get the right channel data for the audio, returning a zero-filled vector if empty
std::vector<int16_t> Audio::dataRight() const
{
    if (m_audioData.empty()) {
        LOG_DEBUG("Audio::dataRight(): Frame is empty, returning zero-filled vector");
        return std::vector<int16_t>(frameSize(), 0);
    }
    
    std::vector<int16_t> dataRight;
    for (int i = 1; i < frameSize(); i += 2) {
        dataRight.push_back(m_audioData[i]);
    }
    return dataRight;
}

// Set the error data for the audio, ensuring it matches the frame size
void Audio::setErrorData(const std::vector<bool> &errorData)
{
    if (errorData.size() != static_cast<size_t>(frameSize())) {
        LOG_ERROR("Audio::setErrorData(): Error data size of {} does not match frame size of {}",
                     errorData.size(), frameSize());
        std::exit(1);
    }
    m_audioErrorData = errorData;
}

// Set the left and right channel error data for the audio, ensuring they match the frame size
void Audio::setErrorDataLeftRight(const std::vector<bool> &errorDataLeft, const std::vector<bool> &errorDataRight)
{
    if (errorDataLeft.size() + errorDataRight.size() != static_cast<size_t>(frameSize())) {
        LOG_ERROR("Audio::setErrorDataLeftRight(): Error data size of {} does not match frame size of {}",
                     errorDataLeft.size() + errorDataRight.size(), frameSize());
        std::exit(1);
    }
    
    m_audioErrorData.clear();
    for (int i = 0; i < frameSize(); i += 2) {
        m_audioErrorData.push_back(errorDataLeft[i]);
        m_audioErrorData.push_back(errorDataRight[i]);
    }
}

// Get the error_data for the audio, returning a zero-filled vector if empty
std::vector<bool> Audio::errorData() const
{
    if (m_audioErrorData.empty()) {
        LOG_DEBUG("Audio::errorData(): Error frame is empty, returning zero-filled vector");
        return std::vector<bool>(frameSize(), false);
    }
    return m_audioErrorData;
}

// Get the left channel error data for the audio, returning a zero-filled vector if empty
std::vector<bool> Audio::errorDataLeft() const
{
    if (m_audioErrorData.empty()) {
        LOG_DEBUG("Audio::errorDataLeft(): Error frame is empty, returning zero-filled vector");
        return std::vector<bool>(frameSize(), false);
    }
    
    std::vector<bool> errorDataLeft;
    for (int i = 0; i < frameSize(); i += 2) {
        errorDataLeft.push_back(m_audioErrorData[i]);
    }
    return errorDataLeft;
}

// Get the right channel error data for the audio, returning a zero-filled vector if empty
std::vector<bool> Audio::errorDataRight() const
{
    if (m_audioErrorData.empty()) {
        LOG_DEBUG("Audio::errorDataRight(): Error frame is empty, returning zero-filled vector");
        return std::vector<bool>(frameSize(), false);
    }
    
    std::vector<bool> errorDataRight;
    for (int i = 1; i < frameSize(); i += 2) {
        errorDataRight.push_back(m_audioErrorData[i]);
    }
    return errorDataRight;
}

// Count the number of errors in the audio
uint32_t Audio::countErrors() const
{
    uint32_t errorCount = 0;
    for (int i = 0; i < frameSize(); ++i) {
        if (m_audioErrorData[i] == true) {
            errorCount++;
        }
    }
    return errorCount;
}

// Count the number of errors in the left channel of the audio
uint32_t Audio::countErrorsLeft() const
{
    uint32_t errorCount = 0;
    for (int i = 0; i < frameSize(); i += 2) {
        if (m_audioErrorData[i] == true) {
            errorCount++;
        }
    }
    return errorCount;
}

// Count the number of errors in the right channel of the audio
uint32_t Audio::countErrorsRight() const
{
    uint32_t errorCount = 0;
    for (int i = 1; i < frameSize(); i += 2) {
        if (m_audioErrorData[i] == true) {
            errorCount++;
        }
    }
    return errorCount;
}

// Check if the audio is full (i.e., has data)
bool Audio::isFull() const
{
    return !isEmpty();
}

// Check if the audio is empty (i.e., has no data)
bool Audio::isEmpty() const
{
    return m_audioData.empty();
}

// Show the audio data and errors in debug
void Audio::showData()
{
    std::string dataString;
    for (int i = 0; i < static_cast<int>(m_audioData.size()); ++i) {
        if (m_audioErrorData[i] == false) {
            char buf[10];
            snprintf(buf, sizeof(buf), "%c%04x ",
                    m_audioData[i] < 0 ? '-' : '+',
                    static_cast<unsigned>(std::abs(m_audioData[i])));
            dataString += buf;
        } else {
            dataString += "XXXXX ";
        }
    }

    // Convert to uppercase for display
    for (char &c : dataString) {
        if (c >= 'a' && c <= 'f') {
            c = c - 'a' + 'A';
        }
    }

    LOG_TRACE("{}", dataString);
}

int Audio::frameSize() const
{
    return 12;
}

void Audio::setConcealedData(const std::vector<bool> &concealedData)
{
    if (static_cast<int>(concealedData.size()) != frameSize()) {
        LOG_ERROR("Audio::setConcealedData(): Concealed data size of {} does not match frame size of {}", concealedData.size(), frameSize());
        std::exit(1);
    }
    m_audioConcealedData = concealedData;
}

std::vector<bool> Audio::concealedData() const
{
    if (m_audioConcealedData.empty()) {
        LOG_DEBUG("Audio::concealedData(): Concealed data is empty, returning zero-filled vector");
        return std::vector<bool>(frameSize(), false);
    }
    return m_audioConcealedData;
}
