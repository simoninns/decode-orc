/*
 * File:        efm_decoder_parameter_contract.h
 * Module:      orc-core
 * Purpose:     Phase 1 contract for EFM Decoder Sink parameters and validation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_EFM_DECODER_PARAMETER_CONTRACT_H
#define ORC_CORE_EFM_DECODER_PARAMETER_CONTRACT_H

#include "stage_parameter.h"
#include <map>
#include <string>
#include <vector>

namespace orc::efm_decoder_config {

std::vector<ParameterDescriptor> get_parameter_descriptors();

std::map<std::string, ParameterValue> default_parameters();

bool validate_and_normalize(
    const std::map<std::string, ParameterValue>& params,
    std::map<std::string, ParameterValue>& normalized,
    std::string& error_message
);

} // namespace orc::efm_decoder_config

#endif // ORC_CORE_EFM_DECODER_PARAMETER_CONTRACT_H
