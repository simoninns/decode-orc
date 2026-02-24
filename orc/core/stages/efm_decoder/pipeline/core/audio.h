/*
 * File:        audio.h
 * Module:      EFM-library
 * Purpose:     Audio frame type class
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef AUDIO_H
#define AUDIO_H

#include <vector>
#include <cstdint>

// Audio class
class Audio
{
public:
    void setData(const std::vector<int16_t> &data);
    void setDataLeftRight(const std::vector<int16_t> &dataLeft, const std::vector<int16_t> &dataRight);
    std::vector<int16_t> data() const;
    std::vector<int16_t> dataLeft() const;
    std::vector<int16_t> dataRight() const;
    void setErrorData(const std::vector<bool> &errorData);
    void setErrorDataLeftRight(const std::vector<bool> &errorDataLeft, const std::vector<bool> &errorDataRight);
    std::vector<bool> errorData() const;
    std::vector<bool> errorDataLeft() const;
    std::vector<bool> errorDataRight() const;
    uint32_t countErrors() const;
    uint32_t countErrorsLeft() const;
    uint32_t countErrorsRight() const;

    void setConcealedData(const std::vector<bool> &paddingData);
    std::vector<bool> concealedData() const;

    bool isFull() const;
    bool isEmpty() const;

    void showData();
    int frameSize() const;

private:
    std::vector<int16_t> m_audioData;
    std::vector<bool> m_audioErrorData;
    std::vector<bool> m_audioConcealedData;
};

#endif // AUDIO_H