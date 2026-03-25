/************************************************************************

    lddecodemetadata.h

    ld-decode-tools TBC library
    Copyright (C) 2018-2020 Simon Inns
    Copyright (C) 2022 Ryan Holtz
    Copyright (C) 2022-2023 Adam Sampson

    This file is part of ld-decode-tools.

    ld-decode-tools is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

************************************************************************/

// Note: Copied from the TBC library so the JSON handling code is local to the application

#ifndef LDDECODEMETADATA_H
#define LDDECODEMETADATA_H

#include <string>
#include <vector>
#include <cstdint>
#include <array>

#include "dropouts.h"

class JsonReader;
class JsonWriter;

// The video system (combination of a line standard and a colour standard)
// Note: If you update this, be sure to update VIDEO_SYSTEM_DEFAULTS also
// Renamed to LdVideoSystem to avoid collision with orc::VideoSystem
enum class LdVideoSystem {
    PAL = 0,    // 625-line PAL
    NTSC,       // 525-line NTSC
    PAL_M,      // 525-line PAL
};

/**
 * @brief Parse video system name from fallback JSON metadata.
 * 
 * Accepts both "PAL_M" (underscore, canonical ld-decode form) and "PAL-M" (hyphen, alternate form).
 * This function is used for deserialization of JSON metadata which may contain alternate representations.
 * For SQLite reads, use video_system_from_string() instead, which only accepts the canonical "PAL_M".
 * 
 * @param name System name string ("PAL", "NTSC", "PAL_M", or "PAL-M")
 * @param system Output parameter set to the parsed VideoSystem
 * @return true if name is recognized, false otherwise
 */
bool parseVideoSystemName(std::string name, LdVideoSystem &system);

class LdDecodeMetaData
{

public:
    // VBI Metadata definition
    struct Vbi {
        bool inUse = false;
        std::array<int32_t, 3> vbiData { 0, 0, 0 };

        void read(JsonReader &reader);
        void write(JsonWriter &writer) const;
    };

    // Video metadata definition
    struct VideoParameters {
        // -- Members stored in the JSON metadata --

        int32_t numberOfSequentialFields = -1;

        LdVideoSystem system = LdVideoSystem::NTSC;
        bool isSubcarrierLocked = false;
        bool isWidescreen = false;

        int32_t colourBurstStart = -1;
        int32_t colourBurstEnd = -1;
        int32_t activeVideoStart = -1;
        int32_t activeVideoEnd = -1;

        int32_t white16bIre = -1;
        int32_t black16bIre = -1;

        int32_t fieldWidth = -1;
        int32_t fieldHeight = -1;
        double sampleRate = -1.0;

        bool isMapped = false;
        std::string tapeFormat = "";

        std::string gitBranch;
        std::string gitCommit;

        // -- Members set by the library --

        // Colour subcarrier frequency in Hz
        double fSC = -1.0;

        // The range of active lines within a frame.
        int32_t firstActiveFieldLine = -1;
        int32_t lastActiveFieldLine = -1;
        int32_t firstActiveFrameLine = -1;
        int32_t lastActiveFrameLine = -1;

        // Flags if our data has been initialized yet
        bool isValid = false;

        void read(JsonReader &reader);
        void write(JsonWriter &writer) const;
    };

    // Specification for customising the range of active lines in VideoParameters.
    struct LineParameters {
        int32_t firstActiveFieldLine = -1;
        int32_t lastActiveFieldLine = -1;
        int32_t firstActiveFrameLine = -1;
        int32_t lastActiveFrameLine = -1;

        void applyTo(VideoParameters &videoParameters);
    };

    // VITS metrics metadata definition
    struct VitsMetrics {
        bool inUse = false;
        double wSNR = 0.0;
        double bPSNR = 0.0;

        void read(JsonReader &reader);
        void write(JsonWriter &writer) const;
    };

    // NTSC Specific metadata definition
    struct ClosedCaption;
    struct Ntsc {
        bool inUse = false;
        bool isFmCodeDataValid = false;
        int32_t fmCodeData = 0;
        bool fieldFlag = false;
        bool isVideoIdDataValid = false;
        int32_t videoIdData = 0;
        bool whiteFlag = false;

        void read(JsonReader &reader, ClosedCaption &closedCaption);
        void write(JsonWriter &writer) const;
    };

    // VITC timecode definition
    struct Vitc {
        bool inUse = false;

        std::array<int32_t, 8> vitcData;

        void read(JsonReader &reader);
        void write(JsonWriter &writer) const;
    };

    // Closed Caption definition
    struct ClosedCaption {
        bool inUse = false;

        int32_t data0 = -1;
        int32_t data1 = -1;

        void read(JsonReader &reader);
        void write(JsonWriter &writer) const;
    };

    // PCM sound metadata definition
    struct PcmAudioParameters {
        double sampleRate = -1.0;
        bool isLittleEndian = false;
        bool isSigned = false;
        int32_t bits = -1;

        // Flags if our data has been initialized yet
        bool isValid = false;

        void read(JsonReader &reader);
        void write(JsonWriter &writer) const;
    };

    // Field metadata definition
    struct Field {
        int32_t seqNo = 0;   // Note: This is the unique primary-key
        bool isFirstField = false;
        int32_t syncConf = 0;
        double medianBurstIRE = 0.0;
        int32_t fieldPhaseID = -1;
        int32_t audioSamples = -1;

        VitsMetrics vitsMetrics;
        Vbi vbi;
        Ntsc ntsc;
        Vitc vitc;
        ClosedCaption closedCaption;
        DropOuts dropOuts;
        bool pad = false;

        double diskLoc = -1;
        int64_t fileLoc = -1;
        int32_t decodeFaults = -1;
        int32_t efmTValues = -1;

        void read(JsonReader &reader);
        void write(JsonWriter &writer) const;
    };

    // CLV timecode (used by frame number conversion methods)
    struct ClvTimecode {
        int32_t hours;
        int32_t minutes;
        int32_t seconds;
        int32_t pictureNumber;
    };

    LdDecodeMetaData();

    // Prevent copying or assignment
    LdDecodeMetaData(const LdDecodeMetaData &) = delete;
    LdDecodeMetaData& operator=(const LdDecodeMetaData &) = delete;

    void clear();
    bool read(std::string fileName);
    bool write(std::string fileName) const;
    void readFields(JsonReader &reader);
    void writeFields(JsonWriter &writer) const;

    const VideoParameters &getVideoParameters();
    void setVideoParameters(const VideoParameters &videoParameters);

    const PcmAudioParameters &getPcmAudioParameters();
    void setPcmAudioParameters(const PcmAudioParameters &pcmAudioParam);

    // Handle line parameters
    void processLineParameters(LdDecodeMetaData::LineParameters &_lineParameters);

    // Get field metadata
    const Field &getField(int32_t sequentialFieldNumber);
    const VitsMetrics &getFieldVitsMetrics(int32_t sequentialFieldNumber);
    const Vbi &getFieldVbi(int32_t sequentialFieldNumber);
    const Ntsc &getFieldNtsc(int32_t sequentialFieldNumber);
    const Vitc &getFieldVitc(int32_t sequentialFieldNumber);
    const ClosedCaption &getFieldClosedCaption(int32_t sequentialFieldNumber);
    const DropOuts &getFieldDropOuts(int32_t sequentialFieldNumber);

    // Set field metadata
    void updateField(const Field &field, int32_t sequentialFieldNumber);
    void updateFieldVitsMetrics(const LdDecodeMetaData::VitsMetrics &vitsMetrics, int32_t sequentialFieldNumber);
    void updateFieldVbi(const LdDecodeMetaData::Vbi &vbi, int32_t sequentialFieldNumber);
    void updateFieldNtsc(const LdDecodeMetaData::Ntsc &ntsc, int32_t sequentialFieldNumber);
    void updateFieldVitc(const LdDecodeMetaData::Vitc &vitc, int32_t sequentialFieldNumber);
    void updateFieldClosedCaption(const LdDecodeMetaData::ClosedCaption &closedCaption, int32_t sequentialFieldNumber);
    void updateFieldDropOuts(const DropOuts &dropOuts, int32_t sequentialFieldNumber);
    void clearFieldDropOuts(int32_t sequentialFieldNumber);

    void appendField(const Field &field);

    void setNumberOfFields(int32_t numberOfFields);
    int32_t getNumberOfFields();
    int32_t getNumberOfFrames();
    int32_t getFirstFieldNumber(int32_t frameNumber);
    int32_t getSecondFieldNumber(int32_t frameNumber);

    void setIsFirstFieldFirst(bool flag);
    bool getIsFirstFieldFirst();

    int32_t convertClvTimecodeToFrameNumber(LdDecodeMetaData::ClvTimecode clvTimeCode);
    LdDecodeMetaData::ClvTimecode convertFrameNumberToClvTimecode(int32_t clvFrameNumber);

    // PCM Analogue audio helper methods
    int32_t getFieldPcmAudioStart(int32_t sequentialFieldNumber);
    int32_t getFieldPcmAudioLength(int32_t sequentialFieldNumber);

    // Video system helper methods
    std::string getVideoSystemDescription() const;

private:
    bool isFirstFieldFirst;
    VideoParameters videoParameters;
    PcmAudioParameters pcmAudioParameters;
    std::vector<Field> fields;
    std::vector<int32_t> pcmAudioFieldStartSampleMap;
    std::vector<int32_t> pcmAudioFieldLengthMap;

    void initialiseVideoSystemParameters();
    int32_t getFieldNumber(int32_t frameNumber, int32_t field);
    void generatePcmAudioMap();
};

#endif // LDDECODEMETADATA_H
