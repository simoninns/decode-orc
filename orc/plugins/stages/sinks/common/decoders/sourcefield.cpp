/*
 * File:        sourcefield.cpp
 * Module:      orc-core
 * Purpose:     Non-owning field view for CVBS_U10_4FSC decoder input
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "sourcefield.h"

#include <utility>

void split_yc_fields(const std::vector<SourceField>& inputFields,
                     std::vector<SourceField>& lumaFields,
                     std::vector<SourceField>& chromaFields) {
  lumaFields.clear();
  chromaFields.clear();
  lumaFields.reserve(inputFields.size());
  chromaFields.reserve(inputFields.size());

  for (const auto& field : inputFields) {
    SourceField lumaField = field;
    lumaField.is_yc = false;
    lumaField.data = field.luma_data;
    lumaField.luma_data = nullptr;
    lumaField.chroma_data = nullptr;
    lumaField.line_ptrs = field.luma_line_ptrs;
    lumaField.luma_line_ptrs.clear();
    lumaField.chroma_line_ptrs.clear();
    lumaFields.push_back(std::move(lumaField));

    SourceField chromaField = field;
    chromaField.is_yc = false;
    chromaField.data = field.chroma_data;
    chromaField.luma_data = nullptr;
    chromaField.chroma_data = nullptr;
    chromaField.line_ptrs = field.chroma_line_ptrs;
    chromaField.luma_line_ptrs.clear();
    chromaField.chroma_line_ptrs.clear();
    chromaFields.push_back(std::move(chromaField));
  }
}
