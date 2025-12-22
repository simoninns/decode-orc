/************************************************************************

    video_parameters.h

    Minimal video parameters structure to replace LdDecodeMetaData
    Copyright (C) 2025 ORC Project

    This file is part of orc-core.

************************************************************************/

#ifndef VIDEO_PARAMETERS_H
#define VIDEO_PARAMETERS_H

#include <cstdint>

enum VideoSystem {
    PAL,
    NTSC,
    PAL_M
};

// Minimal VideoParameters structure extracted from what decoders actually use
struct VideoParameters {
    VideoSystem system;
    
    // Field dimensions
    int32_t fieldWidth;
    int32_t fieldHeight;
    
    // Active video area
    int32_t activeVideoStart;
    int32_t activeVideoEnd;
    int32_t firstActiveFieldLine;
    int32_t lastActiveFieldLine;
    int32_t firstActiveFrameLine;
    int32_t lastActiveFrameLine;
    
    // Color carrier frequency (for PAL/NTSC decoding)
    double fsc;
    
    // Sample rate
    double sampleRate;
    
    // Field order
    bool isFirstFieldFirst;
    
    VideoParameters()
        : system(PAL)
        , fieldWidth(0)
        , fieldHeight(0)
        , activeVideoStart(0)
        , activeVideoEnd(0)
        , firstActiveFieldLine(0)
        , lastActiveFieldLine(0)
        , firstActiveFrameLine(0)
        , lastActiveFrameLine(0)
        , fsc(0.0)
        , sampleRate(0.0)
        , isFirstFieldFirst(true)
    {}
};

// Minimal Field metadata structure
struct FieldMetadata {
    int32_t seqNo;
    bool isFirstField;
    
    FieldMetadata()
        : seqNo(0)
        , isFirstField(true)
    {}
};

#endif // VIDEO_PARAMETERS_H
