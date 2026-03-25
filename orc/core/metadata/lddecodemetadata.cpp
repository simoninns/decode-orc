/************************************************************************

    lddecodemetadata.cpp

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

#include "lddecodemetadata.h"

#include "jsonio.h"

#include <cassert>
#include <fstream>
#include <spdlog/spdlog.h>

// Default values used when configuring VideoParameters for a particular video system.
struct VideoSystemDefaults {
    LdVideoSystem system;
    const char *name;
    double fSC;
    int32_t minActiveFrameLine;
    int32_t firstActiveFieldLine;
    int32_t lastActiveFieldLine;
    int32_t firstActiveFrameLine;
    int32_t lastActiveFrameLine;
};

static constexpr VideoSystemDefaults palDefaults {
    LdVideoSystem::PAL,
    "PAL",
    (283.75 * 15625) + 25,
    2,
    22, 308,
    44, 620,
};

static constexpr VideoSystemDefaults ntscDefaults {
    LdVideoSystem::NTSC,
    "NTSC",
    315.0e6 / 88.0,
    1,
    20, 259,
    40, 525,
};

static constexpr VideoSystemDefaults palMDefaults {
    LdVideoSystem::PAL_M,
    "PAL_M",
    5.0e6 * (63.0 / 88.0) * (909.0 / 910.0),
    ntscDefaults.minActiveFrameLine,
    ntscDefaults.firstActiveFieldLine, ntscDefaults.lastActiveFieldLine,
    ntscDefaults.firstActiveFrameLine, ntscDefaults.lastActiveFrameLine,
};

// These must be in the same order as enum VideoSystem
static constexpr VideoSystemDefaults VIDEO_SYSTEM_DEFAULTS[] = {
    palDefaults,
    ntscDefaults,
    palMDefaults,
};

// Return appropriate defaults for the selected video system
static const VideoSystemDefaults &getSystemDefaults(const LdDecodeMetaData::VideoParameters &videoParameters)
{
    return VIDEO_SYSTEM_DEFAULTS[static_cast<size_t>(videoParameters.system)];
}

// Look up a video system by name.
// Return true and set system if found; if not found, return false.
// For PAL-M, both "PAL_M" (underscore, from ld-decode) and "PAL-M" (hyphen, alternate representation) are accepted.
bool parseVideoSystemName(std::string name, LdVideoSystem &system)
{
    // Search VIDEO_SYSTEM_DEFAULTS for a matching name
    for (const auto &defaults: VIDEO_SYSTEM_DEFAULTS) {
        if (name == defaults.name) {
            system = defaults.system;
            return true;
        }
    }
    
    // Additional fallback: accept "PAL-M" as alternate representation for PAL_M
    if (name == "PAL-M") {
        system = LdVideoSystem::PAL_M;
        return true;
    }
    
    return false;
}

// Read Vbi from JSON
void LdDecodeMetaData::Vbi::read(JsonReader &reader)
{
    reader.beginObject();

    std::string member;
    while (reader.readMember(member)) {
        if (member == "vbiData") {
            reader.beginArray();

            // There should be exactly 3 values, but handle more or less
            unsigned int i = 0;
            while (reader.readElement()) {
                int value;
                reader.read(value);

                if (i < vbiData.size()) vbiData[i++] = value;
            }
            while (i < vbiData.size()) vbiData[i++] = 0;

            reader.endArray();
        } else {
            reader.discard();
        }
    }

    reader.endObject();

    inUse = true;
}

// Write Vbi to JSON
void LdDecodeMetaData::Vbi::write(JsonWriter &writer) const
{
    assert(inUse);

    writer.beginObject();

    // Keep members in alphabetical order
    writer.writeMember("vbiData");
    writer.beginArray();
    for (auto value : vbiData) {
        writer.writeElement();
        writer.write(value);
    }
    writer.endArray();

    writer.endObject();
}

// Read VideoParameters from JSON
void LdDecodeMetaData::VideoParameters::read(JsonReader &reader)
{
    bool isSourcePal = false;
    std::string systemString = "";

    reader.beginObject();

    std::string member;
    while (reader.readMember(member)) {
        if (member == "activeVideoEnd") reader.read(activeVideoEnd);
        else if (member == "activeVideoStart") reader.read(activeVideoStart);
        else if (member == "black16bIre") reader.read(black16bIre);
        else if (member == "colourBurstEnd") reader.read(colourBurstEnd);
        else if (member == "colourBurstStart") reader.read(colourBurstStart);
        else if (member == "fieldHeight") reader.read(fieldHeight);
        else if (member == "fieldWidth") reader.read(fieldWidth);
        else if (member == "gitBranch") reader.read(gitBranch);
        else if (member == "gitCommit") reader.read(gitCommit);
        else if (member == "isMapped") reader.read(isMapped);
        else if (member == "isSourcePal") reader.read(isSourcePal); // obsolete
        else if (member == "isSubcarrierLocked") reader.read(isSubcarrierLocked);
        else if (member == "isWidescreen") reader.read(isWidescreen);
        else if (member == "numberOfSequentialFields") reader.read(numberOfSequentialFields);
        else if (member == "sampleRate") reader.read(sampleRate);
        else if (member == "system") reader.read(systemString);
        else if (member == "white16bIre") reader.read(white16bIre);
        else if (member == "tapeFormat") reader.read(tapeFormat);
        else reader.discard();
    }

    reader.endObject();

    // Work out which video system is being used
    if (systemString == "") {
        // Not specified -- detect based on isSourcePal and fieldHeight
        if (isSourcePal) {
            if (fieldHeight < 300) system = LdVideoSystem::PAL_M;
            else system = LdVideoSystem::PAL;
        } else system = LdVideoSystem::NTSC;
    } else if (!parseVideoSystemName(systemString, system)) {
        reader.throwError("unknown value for videoParameters.system");
    }

    isValid = true;
}

// Write VideoParameters to JSON
void LdDecodeMetaData::VideoParameters::write(JsonWriter &writer) const
{
    assert(isValid);

    writer.beginObject();

    // Keep members in alphabetical order
    writer.writeMember("activeVideoEnd", activeVideoEnd);
    writer.writeMember("activeVideoStart", activeVideoStart);
    writer.writeMember("black16bIre", black16bIre);
    writer.writeMember("colourBurstEnd", colourBurstEnd);
    writer.writeMember("colourBurstStart", colourBurstStart);
    writer.writeMember("fieldHeight", fieldHeight);
    writer.writeMember("fieldWidth", fieldWidth);
    if (!gitBranch.empty()) {
        writer.writeMember("gitBranch", gitBranch);
    }
    if (!gitCommit.empty()) {
        writer.writeMember("gitCommit", gitCommit);
    }
    writer.writeMember("isMapped", isMapped);
    writer.writeMember("isSubcarrierLocked", isSubcarrierLocked);
    writer.writeMember("isWidescreen", isWidescreen);
    writer.writeMember("numberOfSequentialFields", numberOfSequentialFields);
    writer.writeMember("sampleRate", sampleRate);
    writer.writeMember("system", VIDEO_SYSTEM_DEFAULTS[static_cast<size_t>(system)].name);
    writer.writeMember("white16bIre", white16bIre);
	if(!tapeFormat.empty()) {
		writer.writeMember("tapeFormat", tapeFormat);
	}

    writer.endObject();
}

// Read VitsMetrics from JSON
void LdDecodeMetaData::VitsMetrics::read(JsonReader &reader)
{
    reader.beginObject();

    std::string member;
    while (reader.readMember(member)) {
        if (member == "bPSNR") reader.read(bPSNR);
        else if (member == "wSNR") reader.read(wSNR);
        else reader.discard();
    }

    reader.endObject();

    inUse = true;
}

// Write VitsMetrics to JSON
void LdDecodeMetaData::VitsMetrics::write(JsonWriter &writer) const
{
    assert(inUse);

    writer.beginObject();

    // Keep members in alphabetical order
    writer.writeMember("bPSNR", bPSNR);
    writer.writeMember("wSNR", wSNR);

    writer.endObject();
}

// Read Ntsc from JSON
void LdDecodeMetaData::Ntsc::read(JsonReader &reader, ClosedCaption &closedCaption)
{
    reader.beginObject();

    std::string member;
    while (reader.readMember(member)) {
        if (member == "isFmCodeDataValid") reader.read(isFmCodeDataValid);
        else if (member == "fmCodeData") reader.read(fmCodeData);
        else if (member == "fieldFlag") reader.read(fieldFlag);
        else if (member == "isVideoIdDataValid") reader.read(isVideoIdDataValid);
        else if (member == "videoIdData") reader.read(videoIdData);
        else if (member == "whiteFlag") reader.read(whiteFlag);
        else if (member == "ccData0") {
            // rev7 and earlier put ccData0/1 here rather than in cc
            reader.read(closedCaption.data0);
            closedCaption.inUse = true;
        } else if (member == "ccData1") {
            reader.read(closedCaption.data1);
            closedCaption.inUse = true;
        } else {
            reader.discard();
        }
    }

    reader.endObject();

    inUse = true;
}

// Write Ntsc to JSON
void LdDecodeMetaData::Ntsc::write(JsonWriter &writer) const
{
    assert(inUse);

    writer.beginObject();

    // Keep members in alphabetical order
    if (isFmCodeDataValid) {
        writer.writeMember("fieldFlag", fieldFlag);
    }
    if (isFmCodeDataValid) {
        writer.writeMember("fmCodeData", fmCodeData);
    }
    writer.writeMember("isFmCodeDataValid", isFmCodeDataValid);
    if (isVideoIdDataValid) {
        writer.writeMember("videoIdData", videoIdData);
    }
    writer.writeMember("isVideoIdDataValid", isVideoIdDataValid);
    if (whiteFlag) {
        writer.writeMember("whiteFlag", whiteFlag);
    }

    writer.endObject();
}

// Read Vitc from JSON
void LdDecodeMetaData::Vitc::read(JsonReader &reader)
{
    reader.beginObject();

    std::string member;
    while (reader.readMember(member)) {
        if (member == "vitcData") {
            reader.beginArray();

            // There should be exactly 8 values, but handle more or less
            unsigned int i = 0;
            while (reader.readElement()) {
                int value;
                reader.read(value);

                if (i < vitcData.size()) vitcData[i++] = value;
            }
            while (i < vitcData.size()) vitcData[i++] = 0;

            reader.endArray();
        } else {
            reader.discard();
        }
    }

    reader.endObject();

    inUse = true;
}

// Write Vitc to JSON
void LdDecodeMetaData::Vitc::write(JsonWriter &writer) const
{
    assert(inUse);

    writer.beginObject();

    // Keep members in alphabetical order
    writer.writeMember("vitcData");
    writer.beginArray();
    for (auto value : vitcData) {
        writer.writeElement();
        writer.write(value);
    }
    writer.endArray();

    writer.endObject();
}

// Read ClosedCaption from JSON
void LdDecodeMetaData::ClosedCaption::read(JsonReader &reader)
{
    reader.beginObject();

    std::string member;
    while (reader.readMember(member)) {
        if (member == "data0") reader.read(data0);
        else if (member == "data1") reader.read(data1);
        else reader.discard();
    }

    reader.endObject();

    inUse = true;
}

// Write ClosedCaption to JSON
void LdDecodeMetaData::ClosedCaption::write(JsonWriter &writer) const
{
    assert(inUse);

    writer.beginObject();

    // Keep members in alphabetical order
    if (data0 != -1) {
        writer.writeMember("data0", data0);
    }
    if (data1 != -1) {
        writer.writeMember("data1", data1);
    }

    writer.endObject();
}

// Read PcmAudioParameters from JSON
void LdDecodeMetaData::PcmAudioParameters::read(JsonReader &reader)
{
    reader.beginObject();

    std::string member;
    while (reader.readMember(member)) {
        if (member == "bits") reader.read(bits);
        else if (member == "isLittleEndian") reader.read(isLittleEndian);
        else if (member == "isSigned") reader.read(isSigned);
        else if (member == "sampleRate") reader.read(sampleRate);
        else reader.discard();
    }

    reader.endObject();

    isValid = true;
}

// Write PcmAudioParameters to JSON
void LdDecodeMetaData::PcmAudioParameters::write(JsonWriter &writer) const
{
    assert(isValid);

    writer.beginObject();

    // Keep members in alphabetical order
    writer.writeMember("bits", bits);
    writer.writeMember("isLittleEndian", isLittleEndian);
    writer.writeMember("isSigned", isSigned);
    writer.writeMember("sampleRate", sampleRate);

    writer.endObject();
}

// Read Field from JSON
void LdDecodeMetaData::Field::read(JsonReader &reader)
{
    reader.beginObject();

    std::string member;
    while (reader.readMember(member)) {
        if (member == "audioSamples") reader.read(audioSamples);
        else if (member == "cc") closedCaption.read(reader);
        else if (member == "decodeFaults") reader.read(decodeFaults);
        else if (member == "diskLoc") reader.read(diskLoc);
        else if (member == "dropOuts") dropOuts.read(reader);
        else if (member == "efmTValues") reader.read(efmTValues);
        else if (member == "fieldPhaseID") reader.read(fieldPhaseID);
        else if (member == "fileLoc") reader.read(fileLoc);
        else if (member == "isFirstField") reader.read(isFirstField);
        else if (member == "medianBurstIRE") reader.read(medianBurstIRE);
        else if (member == "ntsc") ntsc.read(reader, closedCaption);
        else if (member == "pad") reader.read(pad);
        else if (member == "seqNo") reader.read(seqNo);
        else if (member == "syncConf") reader.read(syncConf);
        else if (member == "vbi") vbi.read(reader);
        else if (member == "vitc") vitc.read(reader);
        else if (member == "vitsMetrics") vitsMetrics.read(reader);
        else reader.discard();
    }

    reader.endObject();
}

// Write Field to JSON
void LdDecodeMetaData::Field::write(JsonWriter &writer) const
{
    writer.beginObject();

    // Keep members in alphabetical order
    if (audioSamples != -1) {
        writer.writeMember("audioSamples", audioSamples);
    }
    if (closedCaption.inUse) {
        writer.writeMember("cc");
        closedCaption.write(writer);
    }
    if (decodeFaults != -1) {
        writer.writeMember("decodeFaults", decodeFaults);
    }
    if (diskLoc != -1) {
        writer.writeMember("diskLoc", diskLoc);
    }
    if (!dropOuts.empty()) {
        writer.writeMember("dropOuts");
        dropOuts.write(writer);
    }
    if (efmTValues != -1) {
        writer.writeMember("efmTValues", efmTValues);
    }
    if (fieldPhaseID != -1) {
        writer.writeMember("fieldPhaseID", fieldPhaseID);
    }
    if (fileLoc != -1) {
        writer.writeMember("fileLoc", fileLoc);
    }
    writer.writeMember("isFirstField", isFirstField);
    writer.writeMember("medianBurstIRE", medianBurstIRE);
    if (ntsc.inUse) {
        writer.writeMember("ntsc");
        ntsc.write(writer);
    }
    writer.writeMember("pad", pad);
    writer.writeMember("seqNo", seqNo);
    writer.writeMember("syncConf", syncConf);
    if (vbi.inUse) {
        writer.writeMember("vbi");
        vbi.write(writer);
    }
    if (vitc.inUse) {
        writer.writeMember("vitc");
        vitc.write(writer);
    }
    if (vitsMetrics.inUse) {
        writer.writeMember("vitsMetrics");
        vitsMetrics.write(writer);
    }

    writer.endObject();
}

LdDecodeMetaData::LdDecodeMetaData()
{
    clear();
}

// Reset the metadata to the defaults
void LdDecodeMetaData::clear()
{
    // Default to the standard still-frame field order (of first field first)
    isFirstFieldFirst = true;

    // Reset the parameters to their defaults
    videoParameters = VideoParameters();
    pcmAudioParameters = PcmAudioParameters();

    fields.clear();
}

// Read all metadata from a JSON file
bool LdDecodeMetaData::read(std::string fileName)
{
    std::ifstream jsonFile(fileName);
    if (jsonFile.fail()) {
        spdlog::error("Opening JSON input file failed: JSON file cannot be opened/does not exist");
        return false;
    }

    clear();

    JsonReader reader(jsonFile);

    try {
        reader.beginObject();

        std::string member;
        while (reader.readMember(member)) {
            if (member == "fields") readFields(reader);
            else if (member == "pcmAudioParameters") pcmAudioParameters.read(reader);
            else if (member == "videoParameters") videoParameters.read(reader);
            else reader.discard();
        }

        reader.endObject();
    } catch (JsonReader::Error &error) {
        spdlog::error("Parsing JSON file failed: {}", error.what());
        return false;
    }

    jsonFile.close();

    // Check we saw VideoParameters - if not, we can't do anything useful!
    if (!videoParameters.isValid) {
        spdlog::error("JSON file invalid: videoParameters object is not defined");
        return false;
    }

    // Check numberOfSequentialFields is consistent
    if (videoParameters.numberOfSequentialFields != static_cast<int32_t>(fields.size())) {
        spdlog::error("JSON file invalid: numberOfSequentialFields does not match fields array");
        return false;
    }

    // Now we know the video system, initialise the rest of VideoParameters
    initialiseVideoSystemParameters();

    // Generate the PCM audio map based on the field metadata
    generatePcmAudioMap();

    return true;
}

// Write all metadata out to a JSON file
bool LdDecodeMetaData::write(std::string fileName) const
{
    std::ofstream jsonFile(fileName);
    if (jsonFile.fail()) {
        spdlog::error("Opening JSON output file failed");
        return false;
    }

    JsonWriter writer(jsonFile);

    writer.beginObject();

    // Keep members in alphabetical order
    writer.writeMember("fields");
    writeFields(writer);
    if (pcmAudioParameters.isValid) {
        writer.writeMember("pcmAudioParameters");
        pcmAudioParameters.write(writer);
    }
    writer.writeMember("videoParameters");
    videoParameters.write(writer);

    writer.endObject();

    jsonFile.close();

    return true;
}

// Read array of Fields from JSON
void LdDecodeMetaData::readFields(JsonReader &reader)
{
    reader.beginArray();

    while (reader.readElement()) {
        Field field;
        field.read(reader);
        fields.push_back(field);
    }

    reader.endArray();
}

// Write array of Fields to JSON
void LdDecodeMetaData::writeFields(JsonWriter &writer) const
{
    writer.beginArray();

    for (const Field &field : fields) {
        writer.writeElement();
        field.write(writer);
    }

    writer.endArray();
}

// This method returns the videoParameters metadata
const LdDecodeMetaData::VideoParameters &LdDecodeMetaData::getVideoParameters()
{
    assert(videoParameters.isValid);
    return videoParameters;
}

// This method sets the videoParameters metadata
void LdDecodeMetaData::setVideoParameters(const LdDecodeMetaData::VideoParameters &_videoParameters)
{
    videoParameters = _videoParameters;
    videoParameters.isValid = true;
}

// This method returns the pcmAudioParameters metadata
const LdDecodeMetaData::PcmAudioParameters &LdDecodeMetaData::getPcmAudioParameters()
{
    assert(pcmAudioParameters.isValid);
    return pcmAudioParameters;
}

// This method sets the pcmAudioParameters metadata
void LdDecodeMetaData::setPcmAudioParameters(const LdDecodeMetaData::PcmAudioParameters &_pcmAudioParameters)
{
    pcmAudioParameters = _pcmAudioParameters;
    pcmAudioParameters.isValid = true;
}

// Based on the video system selected, set default values for the members of
// VideoParameters that aren't obtained from the JSON.
void LdDecodeMetaData::initialiseVideoSystemParameters()
{
    const VideoSystemDefaults &defaults = getSystemDefaults(videoParameters);
    videoParameters.fSC = defaults.fSC;

    // Set default LineParameters
    LdDecodeMetaData::LineParameters lineParameters;
    processLineParameters(lineParameters);
}

// Validate LineParameters and apply them to the VideoParameters
void LdDecodeMetaData::processLineParameters(LdDecodeMetaData::LineParameters &lineParameters)
{
    lineParameters.applyTo(videoParameters);
}

// Validate and apply to a set of VideoParameters
void LdDecodeMetaData::LineParameters::applyTo(LdDecodeMetaData::VideoParameters &videoParameters)
{
    const bool firstFieldLineExists = firstActiveFieldLine != -1;
    const bool lastFieldLineExists = lastActiveFieldLine != -1;
    const bool firstFrameLineExists = firstActiveFrameLine != -1;
    const bool lastFrameLineExists = lastActiveFrameLine != -1;

    const VideoSystemDefaults &defaults = getSystemDefaults(videoParameters);
    const int32_t minFirstFrameLine = defaults.minActiveFrameLine;
    const int32_t defaultFirstFieldLine = defaults.firstActiveFieldLine;
    const int32_t defaultLastFieldLine = defaults.lastActiveFieldLine;
    const int32_t defaultFirstFrameLine = defaults.firstActiveFrameLine;
    const int32_t defaultLastFrameLine = defaults.lastActiveFrameLine;

    // Validate and potentially fix the first active field line.
    if (firstActiveFieldLine < 1 || firstActiveFieldLine > defaultLastFieldLine) {
        if (firstFieldLineExists) {
            spdlog::info("Specified first active field line {} out of bounds (1 to {}), resetting to default ({}).",
                         firstActiveFieldLine, defaultLastFieldLine, defaultFirstFieldLine);
        }
        firstActiveFieldLine = defaultFirstFieldLine;
    }

    // Validate and potentially fix the last active field line.
    if (lastActiveFieldLine < 1 || lastActiveFieldLine > defaultLastFieldLine) {
        if (lastFieldLineExists) {
            spdlog::info("Specified last active field line {} out of bounds (1 to {}), resetting to default ({}).",
                         lastActiveFieldLine, defaultLastFieldLine, defaultLastFieldLine);
        }
        lastActiveFieldLine = defaultLastFieldLine;
    }

    // Range-check the first and last active field lines.
    if (firstActiveFieldLine > lastActiveFieldLine) {
        spdlog::info("Specified last active field line {} is before specified first active field line {}, resetting to defaults ({}-{}).",
                     lastActiveFieldLine, firstActiveFieldLine, defaultFirstFieldLine, defaultLastFieldLine);
        firstActiveFieldLine = defaultFirstFieldLine;
        lastActiveFieldLine = defaultLastFieldLine;
    }

    // Validate and potentially fix the first active frame line.
    if (firstActiveFrameLine < minFirstFrameLine || firstActiveFrameLine > defaultLastFrameLine) {
        if (firstFrameLineExists) {
            spdlog::info("Specified first active frame line {} out of bounds ({} to {}), resetting to default ({}).",
                         firstActiveFrameLine, minFirstFrameLine, defaultLastFrameLine, defaultFirstFrameLine);
        }
        firstActiveFrameLine = defaultFirstFrameLine;
    }

    // Validate and potentially fix the last active frame line.
    if (lastActiveFrameLine < minFirstFrameLine || lastActiveFrameLine > defaultLastFrameLine) {
        if (lastFrameLineExists) {
            spdlog::info("Specified last active frame line {} out of bounds ({} to {}), resetting to default ({}).",
                         lastActiveFrameLine, minFirstFrameLine, defaultLastFrameLine, defaultLastFrameLine);
        }
        lastActiveFrameLine = defaultLastFrameLine;
    }

    // Range-check the first and last active frame lines.
    if (firstActiveFrameLine > lastActiveFrameLine) {
        spdlog::info("Specified last active frame line {} is before specified first active frame line {}, resetting to defaults ({}-{}).",
                     lastActiveFrameLine, firstActiveFrameLine, defaultFirstFrameLine, defaultLastFrameLine);
        firstActiveFrameLine = defaultFirstFrameLine;
        lastActiveFrameLine = defaultLastFrameLine;
    }

    // Store the new values back into videoParameters
    videoParameters.firstActiveFieldLine = firstActiveFieldLine;
    videoParameters.lastActiveFieldLine = lastActiveFieldLine;
    videoParameters.firstActiveFrameLine = firstActiveFrameLine;
    videoParameters.lastActiveFrameLine = lastActiveFrameLine;
}

// This method gets the metadata for the specified sequential field number (indexed from 1 (not 0!))
const LdDecodeMetaData::Field &LdDecodeMetaData::getField(int32_t sequentialFieldNumber)
{
    int32_t fieldNumber = sequentialFieldNumber - 1;
    if (fieldNumber < 0 || fieldNumber >= getNumberOfFields()) {
        spdlog::error("LdDecodeMetaData::getField(): Requested field number {} out of bounds!", sequentialFieldNumber);
    }

    return fields[static_cast<size_t>(fieldNumber)];
}

// This method gets the VITS metrics metadata for the specified sequential field number
const LdDecodeMetaData::VitsMetrics &LdDecodeMetaData::getFieldVitsMetrics(int32_t sequentialFieldNumber)
{
    int32_t fieldNumber = sequentialFieldNumber - 1;
    if (fieldNumber < 0 || fieldNumber >= getNumberOfFields()) {
        spdlog::error("LdDecodeMetaData::getFieldVitsMetrics(): Requested field number {} out of bounds!", sequentialFieldNumber);
    }

    return fields[static_cast<size_t>(fieldNumber)].vitsMetrics;
}

// This method gets the VBI metadata for the specified sequential field number
const LdDecodeMetaData::Vbi &LdDecodeMetaData::getFieldVbi(int32_t sequentialFieldNumber)
{
    int32_t fieldNumber = sequentialFieldNumber - 1;
    if (fieldNumber < 0 || fieldNumber >= getNumberOfFields()) {
        spdlog::error("LdDecodeMetaData::getFieldVbi(): Requested field number {} out of bounds!", sequentialFieldNumber);
    }

    return fields[static_cast<size_t>(fieldNumber)].vbi;
}

// This method gets the NTSC metadata for the specified sequential field number
const LdDecodeMetaData::Ntsc &LdDecodeMetaData::getFieldNtsc(int32_t sequentialFieldNumber)
{
    int32_t fieldNumber = sequentialFieldNumber - 1;
    if (fieldNumber < 0 || fieldNumber >= getNumberOfFields()) {
        spdlog::error("LdDecodeMetaData::getFieldNtsc(): Requested field number {} out of bounds!", sequentialFieldNumber);
    }

    return fields[static_cast<size_t>(fieldNumber)].ntsc;
}

// This method gets the VITC metadata for the specified sequential field number
const LdDecodeMetaData::Vitc &LdDecodeMetaData::getFieldVitc(int32_t sequentialFieldNumber)
{
    int32_t fieldNumber = sequentialFieldNumber - 1;
    if (fieldNumber < 0 || fieldNumber >= getNumberOfFields()) {
        spdlog::error("LdDecodeMetaData::getFieldVitc(): Requested field number {} out of bounds!", sequentialFieldNumber);
    }

    return fields[static_cast<size_t>(fieldNumber)].vitc;
}

// This method gets the Closed Caption metadata for the specified sequential field number
const LdDecodeMetaData::ClosedCaption &LdDecodeMetaData::getFieldClosedCaption(int32_t sequentialFieldNumber)
{
    int32_t fieldNumber = sequentialFieldNumber - 1;
    if (fieldNumber < 0 || fieldNumber >= getNumberOfFields()) {
        spdlog::error("LdDecodeMetaData::getFieldClosedCaption(): Requested field number {} out of bounds!", sequentialFieldNumber);
    }

    return fields[static_cast<size_t>(fieldNumber)].closedCaption;
}

// This method gets the drop-out metadata for the specified sequential field number
const DropOuts &LdDecodeMetaData::getFieldDropOuts(int32_t sequentialFieldNumber)
{
    int32_t fieldNumber = sequentialFieldNumber - 1;
    if (fieldNumber < 0 || fieldNumber >= getNumberOfFields()) {
        spdlog::error("LdDecodeMetaData::getFieldDropOuts(): Requested field number {} out of bounds!", sequentialFieldNumber);
    }

    return fields[static_cast<size_t>(fieldNumber)].dropOuts;
}

// This method sets the field metadata for a field
void LdDecodeMetaData::updateField(const LdDecodeMetaData::Field &field, int32_t sequentialFieldNumber)
{
    int32_t fieldNumber = sequentialFieldNumber - 1;
    if (fieldNumber < 0 || fieldNumber >= getNumberOfFields()) {
        spdlog::error("LdDecodeMetaData::updateFieldVitsMetrics(): Requested field number {} out of bounds!", sequentialFieldNumber);
    }

    fields[static_cast<size_t>(fieldNumber)] = field;
}

// This method sets the field VBI metadata for a field
void LdDecodeMetaData::updateFieldVitsMetrics(const LdDecodeMetaData::VitsMetrics &vitsMetrics, int32_t sequentialFieldNumber)
{
    int32_t fieldNumber = sequentialFieldNumber - 1;
    if (fieldNumber < 0 || fieldNumber >= getNumberOfFields()) {
        spdlog::error("LdDecodeMetaData::updateFieldVitsMetrics(): Requested field number {} out of bounds!", sequentialFieldNumber);
    }

    fields[static_cast<size_t>(fieldNumber)].vitsMetrics = vitsMetrics;
}

// This method sets the field VBI metadata for a field
void LdDecodeMetaData::updateFieldVbi(const LdDecodeMetaData::Vbi &vbi, int32_t sequentialFieldNumber)
{
    int32_t fieldNumber = sequentialFieldNumber - 1;
    if (fieldNumber < 0 || fieldNumber >= getNumberOfFields()) {
        spdlog::error("LdDecodeMetaData::updateFieldVbi(): Requested field number {} out of bounds!", sequentialFieldNumber);
    }

    fields[static_cast<size_t>(fieldNumber)].vbi = vbi;
}

// This method sets the field NTSC metadata for a field
void LdDecodeMetaData::updateFieldNtsc(const LdDecodeMetaData::Ntsc &ntsc, int32_t sequentialFieldNumber)
{
    int32_t fieldNumber = sequentialFieldNumber - 1;
    if (fieldNumber < 0 || fieldNumber >= getNumberOfFields()) {
        spdlog::error("LdDecodeMetaData::updateFieldNtsc(): Requested field number {} out of bounds!", sequentialFieldNumber);
    }

    fields[fieldNumber].ntsc = ntsc;
}

// This method sets the VITC metadata for a field
void LdDecodeMetaData::updateFieldVitc(const LdDecodeMetaData::Vitc &vitc, int32_t sequentialFieldNumber)
{
    int32_t fieldNumber = sequentialFieldNumber - 1;
    if (fieldNumber < 0 || fieldNumber >= getNumberOfFields()) {
        spdlog::error("LdDecodeMetaData::updateFieldVitc(): Requested field number {} out of bounds!", sequentialFieldNumber);
    }

    fields[static_cast<size_t>(fieldNumber)].vitc = vitc;
}

// This method sets the Closed Caption metadata for a field
void LdDecodeMetaData::updateFieldClosedCaption(const LdDecodeMetaData::ClosedCaption &closedCaption, int32_t sequentialFieldNumber)
{
    int32_t fieldNumber = sequentialFieldNumber - 1;
    if (fieldNumber < 0 || fieldNumber >= getNumberOfFields()) {
        spdlog::error("LdDecodeMetaData::updateFieldClosedCaption(): Requested field number {} out of bounds!", sequentialFieldNumber);
    }

    fields[static_cast<size_t>(fieldNumber)].closedCaption = closedCaption;
}

// This method sets the field dropout metadata for a field
void LdDecodeMetaData::updateFieldDropOuts(const DropOuts &dropOuts, int32_t sequentialFieldNumber)
{
    int32_t fieldNumber = sequentialFieldNumber - 1;
    if (fieldNumber < 0 || fieldNumber >= getNumberOfFields()) {
        spdlog::error("LdDecodeMetaData::updateFieldDropOuts(): Requested field number {} out of bounds!", sequentialFieldNumber);
    }

    fields[static_cast<size_t>(fieldNumber)].dropOuts = dropOuts;
}

// This method clears the field dropout metadata for a field
void LdDecodeMetaData::clearFieldDropOuts(int32_t sequentialFieldNumber)
{
    int32_t fieldNumber = sequentialFieldNumber - 1;
    if (fieldNumber < 0 || fieldNumber >= getNumberOfFields()) {
        spdlog::error("LdDecodeMetaData::clearFieldDropOuts(): Requested field number {} out of bounds!", sequentialFieldNumber);
    }

    fields[static_cast<size_t>(fieldNumber)].dropOuts.clear();
}

// This method appends a new field to the existing metadata
void LdDecodeMetaData::appendField(const LdDecodeMetaData::Field &field)
{
    // Ensure appended fields receive contiguous sequential numbering
    LdDecodeMetaData::Field fieldCopy = field;
    fieldCopy.seqNo = static_cast<int32_t>(fields.size()) + 1;
    fields.push_back(fieldCopy);

    videoParameters.numberOfSequentialFields = static_cast<int32_t>(fields.size());
}

// Method to get the available number of fields (according to the metadata)
int32_t LdDecodeMetaData::getNumberOfFields()
{
    return static_cast<int32_t>(fields.size());
}

// Method to set the available number of fields
void LdDecodeMetaData::setNumberOfFields(int32_t numberOfFields)
{
    videoParameters.numberOfSequentialFields = numberOfFields;
}

// Method to get the available number of still-frames
int32_t LdDecodeMetaData::getNumberOfFrames()
{
    int32_t frameOffset = 0;

    // If the first field in the TBC input isn't the expected first field,
    // skip it when counting the number of still-frames
    if (isFirstFieldFirst) {
        if (!getField(1).isFirstField) frameOffset = 1;
    } else {
        if (getField(1).isFirstField) frameOffset = 1;
    }

    return (getNumberOfFields() / 2) - frameOffset;
}

// Method to get the first and second field numbers based on the frame number
// If field = 1 return the firstField, otherwise return second field
int32_t LdDecodeMetaData::getFieldNumber(int32_t frameNumber, int32_t field)
{
    int32_t firstFieldNumber = 0;
    int32_t secondFieldNumber = 0;

    // Verify the frame number
    if (frameNumber < 1) {
        spdlog::error("Invalid frame number, cannot determine fields");
        return -1;
    }

    // Calculate the first and last fields based on the position in the TBC
    if (isFirstFieldFirst) {
        firstFieldNumber = (frameNumber * 2) - 1;
        secondFieldNumber = firstFieldNumber + 1;
    } else {
        secondFieldNumber = (frameNumber * 2) - 1;
        firstFieldNumber = secondFieldNumber + 1;
    }

    while (!getField(firstFieldNumber).isFirstField) {
        firstFieldNumber++;
        secondFieldNumber++;

        if (firstFieldNumber > getNumberOfFields() || secondFieldNumber > getNumberOfFields()) {
            spdlog::error("Attempting to get field number failed - no isFirstField in JSON before end of file");
            firstFieldNumber = -1;
            secondFieldNumber = -1;
            break;
        }
    }

    if (firstFieldNumber > getNumberOfFields()) {
        spdlog::error("LdDecodeMetaData::getFieldNumber(): First field number exceed the available number of fields!");
        firstFieldNumber = -1;
        secondFieldNumber = -1;
    }

    if (secondFieldNumber > getNumberOfFields()) {
        spdlog::error("LdDecodeMetaData::getFieldNumber(): Second field number exceed the available number of fields!");
        firstFieldNumber = -1;
        secondFieldNumber = -1;
    }

    if (secondFieldNumber >= 1 && getField(secondFieldNumber).isFirstField) {
        spdlog::error("LdDecodeMetaData::getFieldNumber(): Both of the determined fields have isFirstField set - the TBC source video is probably broken...");
    }

    if (field == 1) return firstFieldNumber; else return secondFieldNumber;
}

// Method to get the first field number based on the frame number
int32_t LdDecodeMetaData::getFirstFieldNumber(int32_t frameNumber)
{
    return getFieldNumber(frameNumber, 1);
}

// Method to get the second field number based on the frame number
int32_t LdDecodeMetaData::getSecondFieldNumber(int32_t frameNumber)
{
    return getFieldNumber(frameNumber, 2);
}

// Method to set the isFirstFieldFirst flag
void LdDecodeMetaData::setIsFirstFieldFirst(bool flag)
{
    isFirstFieldFirst = flag;
}

// Method to get the isFirstFieldFirst flag
bool LdDecodeMetaData::getIsFirstFieldFirst()
{
    return isFirstFieldFirst;
}

// Method to convert a CLV time code into an equivalent frame number
int32_t LdDecodeMetaData::convertClvTimecodeToFrameNumber(LdDecodeMetaData::ClvTimecode clvTimeCode)
{
    int32_t frameNumber = 0;
    VideoParameters vp = getVideoParameters();

    if (clvTimeCode.hours == -1 || clvTimeCode.minutes == -1 || clvTimeCode.seconds == -1 || clvTimeCode.pictureNumber == -1) {
        return -1;
    }

    if (clvTimeCode.hours != -1) {
        if (vp.system == LdVideoSystem::PAL) frameNumber += clvTimeCode.hours * 3600 * 25;
        else frameNumber += clvTimeCode.hours * 3600 * 30;
    }

    if (clvTimeCode.minutes != -1) {
        if (vp.system == LdVideoSystem::PAL) frameNumber += clvTimeCode.minutes * 60 * 25;
        else frameNumber += clvTimeCode.minutes * 60 * 30;
    }

    if (clvTimeCode.seconds != -1) {
        if (vp.system == LdVideoSystem::PAL) frameNumber += clvTimeCode.seconds * 25;
        else frameNumber += clvTimeCode.seconds * 30;
    }

    if (clvTimeCode.pictureNumber != -1) {
        frameNumber += clvTimeCode.pictureNumber;
    }

    return frameNumber;
}

// Method to convert a frame number into an equivalent CLV timecode
LdDecodeMetaData::ClvTimecode LdDecodeMetaData::convertFrameNumberToClvTimecode(int32_t frameNumber)
{
    ClvTimecode clvTimecode;

    clvTimecode.hours = 0;
    clvTimecode.minutes = 0;
    clvTimecode.seconds = 0;
    clvTimecode.pictureNumber = 0;

    if (getVideoParameters().system == LdVideoSystem::PAL) {
        clvTimecode.hours = frameNumber / (3600 * 25);
        frameNumber -= clvTimecode.hours * (3600 * 25);

        clvTimecode.minutes = frameNumber / (60 * 25);
        frameNumber -= clvTimecode.minutes * (60 * 25);

        clvTimecode.seconds = frameNumber / 25;
        frameNumber -= clvTimecode.seconds * 25;

        clvTimecode.pictureNumber = frameNumber;
    } else {
        clvTimecode.hours = frameNumber / (3600 * 30);
        frameNumber -= clvTimecode.hours * (3600 * 30);

        clvTimecode.minutes = frameNumber / (60 * 30);
        frameNumber -= clvTimecode.minutes * (60 * 30);

        clvTimecode.seconds = frameNumber / 30;
        frameNumber -= clvTimecode.seconds * 30;

        clvTimecode.pictureNumber = frameNumber;
    }

    return clvTimecode;
}

// Method to return a description string for the current video format
std::string LdDecodeMetaData::getVideoSystemDescription() const
{
    return getSystemDefaults(videoParameters).name;
}

// Private method to generate a map of the PCM audio data
void LdDecodeMetaData::generatePcmAudioMap()
{
    pcmAudioFieldStartSampleMap.clear();
    pcmAudioFieldLengthMap.clear();

    spdlog::debug("LdDecodeMetaData::generatePcmAudioMap(): Generating PCM audio map...");

    int32_t numberOfFields = getVideoParameters().numberOfSequentialFields;
    pcmAudioFieldStartSampleMap.resize(static_cast<size_t>(numberOfFields + 1));
    pcmAudioFieldLengthMap.resize(static_cast<size_t>(numberOfFields + 1));

    for (int32_t fieldNo = 0; fieldNo < numberOfFields; fieldNo++) {
        pcmAudioFieldLengthMap[static_cast<size_t>(fieldNo)] = getField(fieldNo + 1).audioSamples;

        if (fieldNo == 0) {
            pcmAudioFieldStartSampleMap[0] = 0;
        } else {
            pcmAudioFieldStartSampleMap[static_cast<size_t>(fieldNo)] =
                pcmAudioFieldStartSampleMap[static_cast<size_t>(fieldNo - 1)] +
                pcmAudioFieldLengthMap[static_cast<size_t>(fieldNo - 1)];
        }
    }
}

// Method to get the start sample location of the specified sequential field number
int32_t LdDecodeMetaData::getFieldPcmAudioStart(int32_t sequentialFieldNumber)
{
    if (static_cast<int32_t>(pcmAudioFieldStartSampleMap.size()) < sequentialFieldNumber) return -1;
    return pcmAudioFieldStartSampleMap[static_cast<size_t>(sequentialFieldNumber - 1)];
}

// Method to get the sample length of the specified sequential field number
int32_t LdDecodeMetaData::getFieldPcmAudioLength(int32_t sequentialFieldNumber)
{
    if (static_cast<int32_t>(pcmAudioFieldLengthMap.size()) < sequentialFieldNumber) return -1;
    return pcmAudioFieldLengthMap[static_cast<size_t>(sequentialFieldNumber - 1)];
}

