/*
* File:        observation_context_interface_mock.h
 * Module:      orc-core-tests
 * Purpose:     Mock to support unit tests
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#ifndef DECODE_ORC_ROOT_OBSERVATION_CONTEXT_INTERFACE_MOCK_H
#define DECODE_ORC_ROOT_OBSERVATION_CONTEXT_INTERFACE_MOCK_H

#include "observation_context_interface.h"
#include <gmock/gmock.h>

using std::string;
using std::vector;
using std::map;
using std::optional;
using orc::FieldID;
using orc::ObservationKey;
using orc::ObservationValue;

// using different namespace from module-under-test so that we can use the same class names in the tests as in the module-under-test
namespace orc_unit_test
{
    /**
     * See https://google.github.io/googletest/gmock_cook_book.html
     */
    class MockObservationContext : public orc::IObservationContext {
    public:
        /*
        *virtual void set(FieldID field_id,
                         const std::string& namespace_,
                         const std::string& key,
                         const orc::ObservationValue& value) = 0;
         */
        MOCK_METHOD(void, set, (FieldID, const string&, const string &, const ObservationValue &), (override));

        /*
        *        virtual std::optional<ObservationValue> get(FieldID field_id,
                                                    const std::string& namespace_,
                                                    const std::string& key) const = 0;
         */
        MOCK_METHOD(optional<ObservationValue>, get, (FieldID, const string&, const string &), (override, const));

        /*
        *        virtual bool has(FieldID field_id,
                         const std::string& namespace_,
                         const std::string& key) const = 0;
         */
        MOCK_METHOD(bool, has, (FieldID, const string&, const string &), (override, const));

        /*
        *virtual std::vector<std::string> get_keys(FieldID field_id,
                                                  const std::string& namespace_) const = 0;
         */
        MOCK_METHOD(vector<string>, get_keys, (FieldID, const string&), (override, const));

        // virtual std::vector<std::string> get_namespaces(FieldID field_id) const = 0;
        MOCK_METHOD(vector<string>, get_namespaces, (FieldID), (override, const));

        /*
        *virtual std::map<std::string, std::map<std::string, ObservationValue>>
        get_all_observations(FieldID field_id) const = 0;
         */
        MOCK_METHOD((map<string, map<string, ObservationValue>>), get_all_observations, (FieldID), (override, const));

        // virtual void clear() = 0;
        MOCK_METHOD(void, clear, (), (override));

        // virtual void clear_field(FieldID field_id) = 0;
        MOCK_METHOD(void, clear_field, (FieldID), (override));

        // virtual void register_schema(const std::vector<ObservationKey>& keys) = 0;
        MOCK_METHOD(void, register_schema, (const vector<ObservationKey>&), (override));

        // virtual void clear_schema() = 0;
        MOCK_METHOD(void, clear_schema, (), (override));
    };
}

#endif //DECODE_ORC_ROOT_OBSERVATION_CONTEXT_INTERFACE_MOCK_H