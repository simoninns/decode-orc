/*
 * File:        reedsolomon.cpp
 * Module:      EFM-library
 * Purpose:     Reed-Solomon CIRC functions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "ezpwd/rs_base"
#include "ezpwd/rs"
#include "reedsolomon.h"
#include "core/logging.h"
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
void ReedSolomon::c1Decode(std::vector<uint8_t> &inputData, std::vector<bool> &errorData,
    std::vector<bool> &paddedData)
{
    // Ensure input data is 32 bytes long
    if (inputData.size() != 32) {
        LOG_ERROR("ReedSolomon::c1Decode - Input data must be 32 bytes long");
        std::exit(1);
    }

    // Just reformat the padded data
    paddedData = std::vector<bool>(paddedData.begin(), paddedData.end() - 4);

    // Convert the std::vector to a std::vector for the ezpwd library
    std::vector<uint8_t> tmpData(inputData.begin(), inputData.end());
    std::vector<int> erasures;
    std::vector<int> position;

    // Convert the errorData into a list of erasure positions
    for (int index = 0; index < errorData.size(); ++index) {
        if (errorData[index])
            erasures.push_back(index);
    }

    if (erasures.size() > 2) {
        // If there are more than 2 erasures, then we can't correct the data - copy the input data
        // to the output data and flag it with errors
        inputData = std::vector<uint8_t>(tmpData.begin(), tmpData.end() - 4);
        errorData.resize(inputData.size());
        std::fill(errorData.begin(), errorData.end(), true);
        ++m_errorC1s;
        return;
    }

    // Decode the data
    int result = c1rs.decode(tmpData, erasures, &position);
    if (result > 2) result = -1;

    // Convert the std::vector back to a std::vector and strip the parity bytes
    inputData = std::vector<uint8_t>(tmpData.begin(), tmpData.end() - 4);
    errorData.resize(inputData.size());

    // If result >= 0, then the Reed-Solomon decode was successful
    if (result >= 0) {
        // Mark all the data as correct
        std::fill(errorData.begin(), errorData.end(), false);

        if (result == 0)
            ++m_validC1s;
        else
            ++m_fixedC1s;
        return;
    }

    // If result < 0, the Reed-Solomon decode completely failed and the data is corrupt
    // Mark all the data as corrupt
    std::fill(errorData.begin(), errorData.end(), true);
    ++m_errorC1s;

    return;
}

// Perform a C2 Reed-Solomon decoding operation on the input data
// This is a (28,24) Reed-Solomon encode - 28 bytes in, 24 bytes out
void ReedSolomon::c2Decode(std::vector<uint8_t> &inputData, std::vector<bool> &errorData,
    std::vector<bool> &paddedData)
{
    // Ensure input data is 28 bytes long
    if (inputData.size() != 28) {
        LOG_ERROR("ReedSolomon::c2Decode - Input data must be 28 bytes long");
        std::exit(1);
    }

    if (errorData.size() != 28) {
        LOG_ERROR("ReedSolomon::c2Decode - Error data must be 28 bytes long");
        std::exit(1);
    }

    // Just reformat the padded data
    std::vector<bool> combined;
    combined.insert(combined.end(), paddedData.begin(), paddedData.begin() + 12);
    combined.insert(combined.end(), paddedData.begin() + 16, paddedData.end());
    paddedData = combined;

    // Convert the std::vector to a std::vector for the ezpwd library
    std::vector<uint8_t> tmpData(inputData.begin(), inputData.end());
    std::vector<int> position;
    std::vector<int> erasures;

    // Convert the errorData into a list of erasure positions
    for (int index = 0; index < errorData.size(); ++index) {
        if (errorData[index] == true)
            erasures.push_back(index);
    }

    // Since we know the erasure positions, we can correct a maximum of 4 errors.  If the number
    // of know input erasures is greater than 4, then we can't correct the data.
    if (erasures.size() > 4) {
        // If there are more than 4 erasures, then we can't correct the data - copy the input data
        // to the output data and flag it with errors
        // if (m_showDebug)
        //     tbcDebugStream().noquote() << "ReedSolomon::c2Decode - Too many erasures to correct";
        inputData = std::vector<uint8_t>(tmpData.begin(), tmpData.begin() + 12);
        inputData.insert(inputData.end(), tmpData.begin() + 16, tmpData.end());
        errorData.resize(inputData.size());
        
        // Set the error data
        std::fill(errorData.begin(), errorData.end(), true);

        ++m_errorC2s;
        return;
    }

    // Decode the data
    int result = c2rs.decode(tmpData, erasures, &position);
    if (result > 2) result = -1;

    // Convert the std::vector back to a std::vector and remove the parity bytes
    // by copying bytes 0-11 and 16-27 to the output data
    inputData = std::vector<uint8_t>(tmpData.begin(), tmpData.begin() + 12);
    inputData.insert(inputData.end(), tmpData.begin() + 16, tmpData.end());
    errorData.resize(inputData.size());

    // If result >= 0, then the Reed-Solomon decode was successful
    if (result >= 0) {
        // Clear the error data
        std::fill(errorData.begin(), errorData.end(), false);

        if (result == 0)
            ++m_validC2s;
        else
            ++m_fixedC2s;
        return;
    }

    // If result < 0, then the Reed-Solomon decode failed and the data should be flagged as corrupt
    // if (m_showDebug)
    //     tbcDebugStream().noquote() << "ReedSolomon::c2Decode - C2 corrupt and could not be fixed"
    //                        << result;
    
    // Set the error data
    std::fill(errorData.begin(), errorData.end(), true);

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
