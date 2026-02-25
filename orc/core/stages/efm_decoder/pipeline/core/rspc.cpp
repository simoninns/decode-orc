/*
 * File:        rspc.cpp
 * Module:      EFM-library
 * Purpose:     Reed-Solomon Product-like Code (RSPC) functions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#if defined(_MSC_VER)
#include <BaseTsd.h>
#include <iso646.h>
using ssize_t = SSIZE_T;
#endif

#include "ezpwd/rs_base"
#include "ezpwd/rs"
#include "rspc.h"
#include <logging.h>
#include <cstdlib>

// ECMA-130 Q and P specific CIRC configuration for Reed-Solomon forward error correction
template < size_t SYMBOLS, size_t PAYLOAD > struct QRS;
template < size_t PAYLOAD > struct QRS<255, PAYLOAD>
    : public __RS(QRS, uint8_t, 255, PAYLOAD, 0x11d, 0, 1, false);

template < size_t SYMBOLS, size_t PAYLOAD > struct PRS;
template < size_t PAYLOAD > struct PRS<255, PAYLOAD>
    : public __RS(PRS, uint8_t, 255, PAYLOAD, 0x11d, 0, 1, false);

Rspc::Rspc()
{}

void Rspc::qParityEcc(std::vector<uint8_t> &inputData, std::vector<uint8_t> &errorData)
{
    // Initialise the RS error corrector
    QRS<255,255-2> qrs; // Up to 251 symbols data load with 2 symbols parity RS(45,43)

    // Keep track of the number of successful corrections
    int32_t successfulCorrections = 0;

    // RS code is Q(45,43)
    // There are 104 bytes of Q-Parity (52 code words)
    // Each Q field covers 12 to 2248 = 2236 bytes (2 * 1118)
    // 2236 / 43 = 52 Q-parity words (= 104 Q-parity bytes)
    //
    // Calculations are based on ECMA-130 Annex A

    uint8_t* uF1Data = reinterpret_cast<uint8_t*>(inputData.data());
    uint8_t* uF1Erasures = reinterpret_cast<uint8_t*>(errorData.data());

    // Ignore the 12 sync bytes
    uF1Data += 12;
    uF1Erasures += 12;

    // Store the data and erasures in the form expected by the ezpwd library
    std::vector<uint8_t> qField;
    std::vector<int> qFieldErasures;
    qField.resize(45); // 43 + 2 parity bytes = 45

    // evenOdd = 0 = LSBs / evenOdd = 1 = MSBs
    for (int32_t evenOdd = 0; evenOdd < 2; evenOdd++) {
        for (int32_t Nq = 0; Nq < 26; Nq++) {
            qFieldErasures.clear();
            for (int32_t Mq = 0; Mq < 43; Mq++) {
                // Get 43 byte codeword location
                int32_t Vq = 2 * ((44 * Mq + 43 * Nq) % 1118) + evenOdd;
                qField[static_cast<size_t>(Mq)] = uF1Data[Vq];

                // Get codeword erasures if present
                if (uF1Erasures[Vq] == 1) qFieldErasures.push_back(Mq);
            }
            // Get 2 byte parity location
            int32_t qParityByte0 = 2 * ((43 * 26 + Nq) % 1118) + evenOdd;
            int32_t qParityByte1 = 2 * ((44 * 26 + Nq) % 1118) + evenOdd;

            // Note: Q-Parity data starts at 12 + 2236
            qField[43] = uF1Data[qParityByte0 + 2236];
            qField[44] = uF1Data[qParityByte1 + 2236];

            // Perform RS decode/correction
            if (qFieldErasures.size() > 2) qFieldErasures.clear();
            std::vector<int> position;
            int fixed = -1;
            fixed = qrs.decode(qField, qFieldErasures, &position);

            // If correction was successful add to success counter
            // and copy back the corrected data
            if (fixed >= 0) {
                successfulCorrections++;

                // Here we use the calculation in reverse to put the corrected
                // data back into it's original position
                for (int32_t Mq = 0; Mq < 43; Mq++) {
                    int32_t Vq = 2 * ((44 * Mq + 43 * Nq) % 1118) + evenOdd;
                    uF1Data[Vq] = qField[static_cast<size_t>(Mq)];
                }
            }
        }
    }

    // Reset the pointers
    uF1Data -= 12;
    uF1Erasures -= 12;

    // Show Q-Parity correction result to debug
    if (successfulCorrections >= 52) {
        // Q-Parity correction successful
    } else {
        LOG_DEBUG("Rspc::qParityEcc(): Q-Parity correction failed! Got {} correct out of 52 possible codewords", successfulCorrections);
    }
}

void Rspc::pParityEcc(std::vector<uint8_t> &inputData, std::vector<uint8_t> &errorData)
{
    // Initialise the RS error corrector
    PRS<255,255-2> prs; // Up to 251 symbols data load with 2 symbols parity RS(26,24)

    // Keep track of the number of successful corrections
    int32_t successfulCorrections = 0;

    uint8_t* uF1Data = reinterpret_cast<uint8_t*>(inputData.data());
    uint8_t* uF1Erasures = reinterpret_cast<uint8_t*>(errorData.data());

    // RS code is P(26,24)
    // There are 172 bytes of P-Parity (86 code words)
    // Each P field covers 12 to 2076 = 2064 bytes (2 * 1032)
    // 2064 / 24 = 86 P-parity words (= 172 P-parity bytes)
    //
    // Calculations are based on ECMA-130 Annex A

    // Ignore the 12 sync bytes
    uF1Data += 12;
    uF1Erasures += 12;

    // Store the data and erasures in the form expected by the ezpwd library
    std::vector<uint8_t> pField;
    std::vector<int> pFieldErasures;
    pField.resize(26); // 24 + 2 parity bytes = 26

    // evenOdd = 0 = LSBs / evenOdd = 1 = MSBs
    for (int32_t evenOdd = 0; evenOdd < 2; evenOdd++) {
        for (int32_t Np = 0; Np < 43; Np++) {
            pFieldErasures.clear();
            for (int32_t Mp = 0; Mp < 26; Mp++) {
                // Get 24 byte codeword location + 2 P-parity bytes
                int32_t Vp = 2 * (43 * Mp + Np) + evenOdd;
                pField[static_cast<size_t>(Mp)] = uF1Data[Vp];

                // Get codeword erasures if present
                if (uF1Erasures[Vp] == 1) pFieldErasures.push_back(Mp);
            }

            // Perform RS decode/correction
            if (pFieldErasures.size() > 2) pFieldErasures.clear();
            std::vector<int> position;
            int fixed = -1;

            fixed = prs.decode(pField, pFieldErasures, &position);

            // If correction was successful add to success counter
            // and copy back the corrected data
            if (fixed >= 0) {
                successfulCorrections++;

                // Here we use the calculation in reverse to put the corrected
                // data back into it's original position
                for (int32_t Mp = 0; Mp < 24; Mp++) {
                    int32_t Vp = 2 * (43 * Mp + Np) + evenOdd;
                    uF1Data[Vp] = pField[static_cast<size_t>(Mp)];
                }
            }
        }
    }

    // Reset the pointers
    uF1Data -= 12;
    uF1Erasures -= 12;

    // Show P-Parity correction result to debug
    if (successfulCorrections >= 86) {
        // P-Parity correction successful
    } else {
        LOG_DEBUG("Rspc::pParityEcc(): P-Parity correction failed! Got {} correct out of 86 possible codewords", successfulCorrections);
    }
}
