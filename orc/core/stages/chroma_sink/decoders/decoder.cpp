/************************************************************************

    decoder.cpp

    ld-chroma-decoder - Colourisation filter for ld-decode
    Copyright (C) 2019-2021 Adam Sampson

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

#include "decoder.h"

#include "decoderpool.h"

int32_t Decoder::getLookBehind() const
{
    return 0;
}

int32_t Decoder::getLookAhead() const
{
    return 0;
}

DecoderThread::DecoderThread(std::atomic<bool>& _abort, DecoderPool& _decoderPool)
    : abort(_abort), decoderPool(_decoderPool), outputWriter(_decoderPool.getOutputWriter())
{
}

void DecoderThread::run()
{
    // Input and output data
    std::vector<SourceField> inputFields;
    std::vector<ComponentFrame> componentFrames;
    QVector<OutputFrame> outputFrames;

    while (!abort) {
        // Get the next batch of fields to process
        int32_t startFrameNumber, startIndex, endIndex;
        if (!decoderPool.getInputFrames(startFrameNumber, inputFields, startIndex, endIndex)) {
            // No more input frames -- exit
            break;
        }

        // Adjust the temporary arrays to the right size
        const int32_t numFrames = (endIndex - startIndex) / 2;
        componentFrames.resize(numFrames);
        outputFrames.resize(numFrames);

        // Decode the fields to component frames
        decodeFrames(inputFields, startIndex, endIndex, componentFrames);

        // Convert the component frames to the output format
        for (int32_t i = 0; i < numFrames; i++) {
            outputWriter.convert(componentFrames[i], outputFrames[i]);
        }

        // Write the frames to the output file
        if (!decoderPool.putOutputFrames(startFrameNumber, outputFrames)) {
            abort = true;
            break;
        }
    }
}
