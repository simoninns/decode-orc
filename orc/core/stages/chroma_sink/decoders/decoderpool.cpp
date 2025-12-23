/************************************************************************

    decoderpool.cpp

    ld-chroma-decoder - Colourisation filter for ld-decode
    Copyright (C) 2018-2019 Simon Inns
    Copyright (C) 2021 Phillip Blucas
    Copyright (C) 2021 Adam Sampson

    This file is part of ld-decode-tools.

    ld-chroma-decoder is free software: you can redistribute it and/or
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

#include "decoderpool.h"
#include <iostream>
#include <fstream>
#include <algorithm>

DecoderPool::DecoderPool(Decoder &_decoder, std::string _inputFileName,
                         LdDecodeMetaData &_ldDecodeMetaData,
                         OutputWriter::Configuration &_outputConfig, std::string _outputFileName,
                         int32_t _startFrame, int32_t _length, int32_t _maxThreads)
    : decoder(_decoder), inputFileName(_inputFileName),
      outputConfig(_outputConfig), outputFileName(_outputFileName),
      startFrame(_startFrame), length(_length), maxThreads(_maxThreads),
      abort(false), ldDecodeMetaData(_ldDecodeMetaData)
{
}

Decoder& DecoderPool::getDecoder() { return decoder; }

bool DecoderPool::process()
{
    LdDecodeMetaData::VideoParameters videoParameters = ldDecodeMetaData.getVideoParameters();

    // Configure the OutputWriter, adjusting videoParameters
    outputWriter.updateConfiguration(videoParameters, outputConfig);
    outputWriter.printOutputInfo();

    // Configure the decoder, and check that it can accept this video
    if (!decoder.configure(videoParameters)) {
        return false;
    }

    // Get the decoder's lookbehind/lookahead requirements
    decoderLookBehind = decoder.getLookBehind();
    decoderLookAhead = decoder.getLookAhead();

    // Open the source video file
    if (!sourceVideo.open(QString::fromStdString(inputFileName), videoParameters.fieldWidth * videoParameters.fieldHeight)) {
        // Could not open source video file
        std::cout << "INFO: Unable to open ld-decode video file" << std::endl;
        return false;
    }

    // If no startFrame parameter was specified, set the start frame to 1
    if (startFrame == -1) startFrame = 1;

    if (startFrame > ldDecodeMetaData.getNumberOfFrames()) {
        std::cout << "INFO: Specified start frame is out of bounds, only " << ldDecodeMetaData.getNumberOfFrames() << " frames available" << std::endl;
        return false;
    }

    // If no length parameter was specified set the length to the number of available frames
    if (length == -1) {
        length = ldDecodeMetaData.getNumberOfFrames() - (startFrame - 1);
    } else {
        if (length + (startFrame - 1) > ldDecodeMetaData.getNumberOfFrames()) {
            std::cout << "INFO: Specified length of " << length << " exceeds the number of available frames, setting to " << ldDecodeMetaData.getNumberOfFrames() - (startFrame - 1) << std::endl;
            length = ldDecodeMetaData.getNumberOfFrames() - (startFrame - 1);
        }
    }

    // Open the output file
    if (outputFileName == "-") {
        // stdout not supported with std::ofstream approach
        // This is used by standalone tool, not by orc-core integration
        std::cerr << "ERROR: stdout output not supported in this mode" << std::endl;
        sourceVideo.close();
        return false;
    } else {
        // Open output file
        targetVideo.open(outputFileName, std::ios::binary | std::ios::out);
        if (!targetVideo.is_open()) {
            // Failed to open output file
            std::cerr << "ERROR: Could not open " << outputFileName << " for output" << std::endl;
            sourceVideo.close();
            return false;
        }
    }

    // Write the stream header (if there is one)
    const std::string streamHeader = outputWriter.getStreamHeader();
    if (!streamHeader.empty()) {
        targetVideo.write(streamHeader.data(), streamHeader.size());
        if (!targetVideo.good()) {
            std::cerr << "ERROR: Writing to the output video file failed" << std::endl;
            return false;
        }
    }

    std::cout << "INFO: Using " << maxThreads << " threads" << std::endl;
    std::cout << "INFO: Processing from start frame #" << startFrame << " with a length of " << length << " frames" << std::endl;

    // Initialise processing state
    inputFrameNumber = startFrame;
    outputFrameNumber = startFrame;
    lastFrameNumber = length + (startFrame - 1);
    totalTimerStart = std::chrono::steady_clock::now();

    // Start a vector of filtering threads to process the video
    std::vector<std::thread> threads;
    threads.reserve(maxThreads);
    for (int32_t i = 0; i < maxThreads; i++) {
        threads.push_back(decoder.makeThread(abort, *this));
    }

    // Wait for the workers to finish
    for (auto& thread : threads) {
        thread.join();
    }

    // Did any of the threads abort?
    if (abort) {
        sourceVideo.close();
        targetVideo.close();
        return false;
    }

    // Check we've processed all the frames, now the workers have finished
    if (inputFrameNumber != (lastFrameNumber + 1) || outputFrameNumber != (lastFrameNumber + 1)
        || !pendingOutputFrames.empty()) {
        std::cerr << "ERROR: Incorrect state at end of processing" << std::endl;
        sourceVideo.close();
        targetVideo.close();
        return false;
    }

    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - totalTimerStart
    ).count();
    double totalSecs = static_cast<double>(total_ms) / 1000.0;
    std::cout << "INFO: Processing complete - " << length << " frames in " << totalSecs << " seconds ("
              << length / totalSecs << " FPS)" << std::endl;

    // Close the source video
    sourceVideo.close();

    // Close the target video
    targetVideo.close();

    return true;
}

bool DecoderPool::getInputFrames(int32_t &startFrameNumber, std::vector<SourceField> &fields, int32_t &startIndex, int32_t &endIndex)
{
    std::lock_guard<std::mutex> locker(inputMutex);

    // Work out a reasonable batch size to provide work for all threads.
    // This assumes that the synchronisation to get a new batch is less
    // expensive than computing a single frame, so a batch size of 1 is
    // reasonable.
    const int32_t maxBatchSize = std::min(DEFAULT_BATCH_SIZE, std::max(1, length / maxThreads));

    // Work out how many frames will be in this batch
    int32_t batchFrames = std::min(maxBatchSize, lastFrameNumber + 1 - inputFrameNumber);
    if (batchFrames == 0) {
        // No more input frames
        return false;
    }

    // Advance the frame number
    startFrameNumber = inputFrameNumber;
    inputFrameNumber += batchFrames;

    // Load the fields
    SourceField::loadFields(sourceVideo, ldDecodeMetaData,
                            startFrameNumber, batchFrames, decoderLookBehind, decoderLookAhead,
                            fields, startIndex, endIndex);

    return true;
}

bool DecoderPool::putOutputFrames(int32_t startFrameNumber, const std::vector<OutputFrame> &outputFrames)
{
    std::lock_guard<std::mutex> locker(outputMutex);

    for (int32_t i = 0; i < static_cast<int32_t>(outputFrames.size()); i++) {
        if (!putOutputFrame(startFrameNumber + i, outputFrames[i])) {
            return false;
        }
    }

    return true;
}

// Write one output frame. You must hold outputMutex to call this.
//
// The worker threads will complete frames in an arbitrary order, so we can't
// just write the frames to the output file directly. Instead, we keep a map of
// frames that haven't yet been written; when a new frame comes in, we check
// whether we can now write some of them out.
//
// Returns true on success, false on failure.
bool DecoderPool::putOutputFrame(int32_t frameNumber, const OutputFrame &outputFrame)
{
    // Put this frame into the map
    pendingOutputFrames[frameNumber] = outputFrame;

    // Write out as many frames as possible
    while (pendingOutputFrames.find(outputFrameNumber) != pendingOutputFrames.end()) {
        const OutputFrame& outputData = pendingOutputFrames.at(outputFrameNumber);

        // Write the frame header (if there is one)
        const std::string frameHeader = outputWriter.getFrameHeader();
        if (!frameHeader.empty()) {
            targetVideo.write(frameHeader.data(), frameHeader.size());
            if (!targetVideo.good()) {
                std::cerr << "ERROR: Writing to the output video file failed" << std::endl;
                return false;
            }
        }

        // Write the frame data
        targetVideo.write(reinterpret_cast<const char *>(outputData.data()), outputData.size() * 2);
        if (!targetVideo.good()) {
            std::cerr << "ERROR: Writing to the output video file failed" << std::endl;
            return false;
        }

        pendingOutputFrames.erase(outputFrameNumber);
        outputFrameNumber++;

        const int32_t outputCount = outputFrameNumber - startFrame;
        if ((outputCount % 32) == 0) {
            // Show an update to the user
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - totalTimerStart
            ).count();
            double fps = outputCount / (static_cast<double>(elapsed_ms) / 1000.0);
            std::cout << "INFO: " << outputCount << " frames processed - " << fps << " FPS" << std::endl;
        }
    }

    return true;
}
