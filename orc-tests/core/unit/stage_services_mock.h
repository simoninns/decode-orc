/*
 * File:        stage_services_mock.h
 * Module:      orc-core-tests
 * Purpose:     Google Mock for IStageServices and SDK file writer interfaces
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#include <gmock/gmock.h>
#include <orc/plugin/orc_stage_services.h>

class MockFileWriterUint8 : public orc::IFileWriterUint8
{
public:
    MOCK_METHOD(bool, open, (const std::string& filepath), (override));
    MOCK_METHOD(void, write, (const uint8_t* data, size_t count), (override));
    MOCK_METHOD(void, write, (const std::vector<uint8_t>& data), (override));
    MOCK_METHOD(void, flush, (), (override));
    MOCK_METHOD(void, close, (), (override));
};

class MockFileWriterUint16 : public orc::IFileWriterUint16
{
public:
    MOCK_METHOD(bool, open, (const std::string& filepath), (override));
    MOCK_METHOD(void, write, (const uint16_t* data, size_t count), (override));
    MOCK_METHOD(void, write, (const std::vector<uint16_t>& data), (override));
    MOCK_METHOD(void, flush, (), (override));
    MOCK_METHOD(void, close, (), (override));
};

class MockStageServices : public orc::IStageServices
{
public:
    MOCK_METHOD(std::shared_ptr<orc::IFileWriterUint8>, create_buffered_file_writer_uint8,
                (size_t buffer_size), (override));
    MOCK_METHOD(std::shared_ptr<orc::IFileWriterUint16>, create_buffered_file_writer_uint16,
                (size_t buffer_size), (override));
};
