/*
 * File:        reedsolomon.cpp
 * Purpose:     EFM-library - Reed-Solomon CIRC functions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "ezpwd/rs_base"
#include "ezpwd/rs"
#include "reedsolomon.h"
#include "logging.h"
#include <cstdlib>

// ezpwd C1 ECMA-130 CIRC configuration
template <size_t SYMBOLS, size_t PAYLOAD>
struct C1RS;
template <size_t PAYLOAD>
struct C1RS<255, PAYLOAD> : public __RS(C1RS, uint8_t, 255, PAYLOAD, 0x11D, 0, 1, false);

C1RS<255, 255 - 4> c1rs;

// ezpwd C2 ECMA-130 CIRC configuration
template <size_t SYMBOLS, size_t PAYLOAD>
struct C2RS;
template <size_t PAYLOAD>
struct C2RS<255, PAYLOAD> : public __RS(C2RS, uint8_t, 255, PAYLOAD, 0x11D, 0, 1, false);

C2RS<255, 255 - 4> c2rs;

ReedSolomon::ReedSolomon()
{
    // Initialise statistics
    m_validC1s = 0;
    m_fixedC1s = 0;
    m_errorC1s = 0;

    m_validC2s = 0;
    m_fixedC2s = 0;
    m_errorC2s = 0;
}

// Perform a C1 Reed-Solomon decoding operation on the input data
// This is a (32,28) Reed-Solomon encode - 32 bytes in, 28 bytes out
void ReedSolomon::c1Decode(std::vector<uint8_t> &inputData, std::vector<uint8_t> &errorData,
    std::vector<uint8_t> &paddedData)
{
    // Ensure input data is 32 bytes long
    if (inputData.size() != 32) {
        ORC_LOG_ERROR("ReedSolomon::c1Decode - Input data must be 32 bytes long");
        std::exit(1);
    }

    // Trim the parity bytes from the padded data (32 → 28)
    paddedData.resize(paddedData.size() - 4);

    // Copy input for the ezpwd decoder (which modifies in place)
    std::vector<uint8_t> tmpData = inputData;
    std::vector<int> erasures;
    std::vector<int> position;

    // Convert the errorData into a list of erasure positions
    for (int index = 0; index < static_cast<int>(errorData.size()); ++index) {
        if (errorData[index])
            erasures.push_back(index);
    }

    if (erasures.size() > 2) {
        // If there are more than 2 erasures, then we can't correct the data - copy the input data
        // to the output data and flag it with errors
        inputData.assign(tmpData.begin(), tmpData.end() - 4);
        errorData.assign(inputData.size(), 1);
        ++m_errorC1s;
        return;
    }

    // Decode the data
    int result = c1rs.decode(tmpData, erasures, &position);
    if (result > 2) result = -1;

    // Strip the parity bytes (32 → 28)
    inputData.assign(tmpData.begin(), tmpData.end() - 4);
    errorData.resize(inputData.size());

    // If result >= 0, then the Reed-Solomon decode was successful
    if (result >= 0) {
        // Mark all the data as correct
        std::fill(errorData.begin(), errorData.end(), uint8_t(0));

        if (result == 0)
            ++m_validC1s;
        else
            ++m_fixedC1s;
        return;
    }

    // If result < 0, the Reed-Solomon decode completely failed and the data is corrupt
    // Mark all the data as corrupt
    std::fill(errorData.begin(), errorData.end(), uint8_t(1));
    ++m_errorC1s;

    return;
}

// Perform a C2 Reed-Solomon decoding operation on the input data
// This is a (28,24) Reed-Solomon encode - 28 bytes in, 24 bytes out
void ReedSolomon::c2Decode(std::vector<uint8_t> &inputData, std::vector<uint8_t> &errorData,
    std::vector<uint8_t> &paddedData)
{
    // Ensure input data is 28 bytes long
    if (inputData.size() != 28) {
        ORC_LOG_ERROR("ReedSolomon::c2Decode - Input data must be 28 bytes long");
        std::exit(1);
    }

    if (errorData.size() != 28) {
        ORC_LOG_ERROR("ReedSolomon::c2Decode - Error data must be 28 bytes long");
        std::exit(1);
    }

    // Remove parity positions 12-15 from paddedData (28 → 24)
    paddedData.erase(paddedData.begin() + 12, paddedData.begin() + 16);

    // Copy input for the ezpwd decoder (which modifies in place)
    std::vector<uint8_t> tmpData = inputData;
    std::vector<int> position;
    std::vector<int> erasures;

    // Convert the errorData into a list of erasure positions
    for (int index = 0; index < static_cast<int>(errorData.size()); ++index) {
        if (errorData[index] != 0)
            erasures.push_back(index);
    }

    // Since we know the erasure positions, we can correct a maximum of 4 errors.  If the number
    // of know input erasures is greater than 4, then we can't correct the data.
    if (erasures.size() > 4) {
        // Remove parity byte positions 12-15 and assign result
        inputData.erase(inputData.begin() + 12, inputData.begin() + 16);
        errorData.assign(inputData.size(), 1);
        ++m_errorC2s;
        return;
    }

    // Decode the data
    int result = c2rs.decode(tmpData, erasures, &position);
    if (result > 2) result = -1;

    // Remove parity byte positions 12-15 from decoded data
    tmpData.erase(tmpData.begin() + 12, tmpData.begin() + 16);
    inputData = std::move(tmpData);
    errorData.resize(inputData.size());

    // If result >= 0, then the Reed-Solomon decode was successful
    if (result >= 0) {
        // Clear the error data
        std::fill(errorData.begin(), errorData.end(), uint8_t(0));

        if (result == 0)
            ++m_validC2s;
        else
            ++m_fixedC2s;
        return;
    }

    // If result < 0, then the Reed-Solomon decode failed and the data should be flagged as corrupt
    // Set the error data
    std::fill(errorData.begin(), errorData.end(), uint8_t(1));

    ++m_errorC2s;
    return;
}

// Getter functions for the statistics
int32_t ReedSolomon::validC1s() const
{
    return m_validC1s;
}

int32_t ReedSolomon::fixedC1s() const
{
    return m_fixedC1s;
}

int32_t ReedSolomon::errorC1s() const
{
    return m_errorC1s;
}

int32_t ReedSolomon::validC2s() const
{
    return m_validC2s;
}

int32_t ReedSolomon::fixedC2s() const
{
    return m_fixedC2s;
}

int32_t ReedSolomon::errorC2s() const
{
    return m_errorC2s;
}
