/*
 * File:        frame.h
 * Module:      EFM-library
 * Purpose:     EFM Frame type classes
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef FRAME_H
#define FRAME_H

#include <vector>
#include <cstdint>
#include <fstream>

// Frame class - base class for F1, F2, and F3 frames
class Frame
{
public:
    virtual ~Frame() {} // Virtual destructor
    virtual int frameSize() const = 0; // Pure virtual function to get frame size

    virtual void setData(const std::vector<uint8_t> &data);
    virtual std::vector<uint8_t> data() const;
    virtual const std::vector<uint8_t> &dataRef() const;

    virtual void setErrorData(const std::vector<bool> &errorData);
    virtual std::vector<bool> errorData() const;
    virtual const std::vector<bool> &errorDataRef() const;
    virtual void setErrorDataBytes(const std::vector<uint8_t> &errorData);
    virtual std::vector<uint8_t> errorDataBytes() const;
    virtual const std::vector<uint8_t> &errorDataBytesRef() const;
    virtual uint32_t countErrors() const;

    virtual void setPaddedData(const std::vector<bool> &errorData);
    virtual std::vector<bool> paddedData() const;
    virtual const std::vector<bool> &paddedDataRef() const;
    virtual void setPaddedDataBytes(const std::vector<uint8_t> &paddedData);
    virtual std::vector<uint8_t> paddedDataBytes() const;
    virtual const std::vector<uint8_t> &paddedDataBytesRef() const;
    virtual uint32_t countPadded() const;

    bool isFull() const;
    bool isEmpty() const;

protected:
    std::vector<uint8_t> m_frameData;
    std::vector<uint8_t> m_frameErrorData;
    std::vector<uint8_t> m_framePaddedData;
    mutable std::vector<uint8_t> m_defaultFrameData;
    mutable std::vector<uint8_t> m_defaultFrameErrorData;
    mutable std::vector<uint8_t> m_defaultFramePaddedData;
    mutable std::vector<bool> m_defaultFrameErrorDataBoolCache;
    mutable std::vector<bool> m_defaultFramePaddedDataBoolCache;
    mutable std::vector<bool> m_frameErrorDataBoolCache;
    mutable std::vector<bool> m_framePaddedDataBoolCache;
};

class Data24 : public Frame
{
public:
    Data24();
    int frameSize() const override;
    void showData();
    void setData(const std::vector<uint8_t> &data) override;
    void setErrorData(const std::vector<bool> &errorData) override;
};

class F1Frame : public Frame
{
public:
    F1Frame();
    int frameSize() const override;
    void showData();
};

class F2Frame : public Frame
{
public:
    F2Frame();
    int frameSize() const override;
    void showData();
};

class F3Frame : public Frame
{
public:
    enum F3FrameType { Subcode, Sync0, Sync1 };

    F3Frame();
    int frameSize() const override;

    void setFrameTypeAsSubcode(uint8_t subcode);
    void setFrameTypeAsSync0();
    void setFrameTypeAsSync1();

    F3FrameType f3FrameType() const;
    std::string f3FrameTypeAsString() const;
    uint8_t subcodeByte() const;

    void showData();

private:
    F3FrameType m_f3FrameType;
    uint8_t m_subcodeByte;
};

#endif // FRAME_H