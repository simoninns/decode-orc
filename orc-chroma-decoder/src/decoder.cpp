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

qint32 Decoder::getLookBehind() const
{
    return 0;
}

qint32 Decoder::getLookAhead() const
{
    return 0;
}

DecoderThread::DecoderThread(QAtomicInt& _abort, DecoderPool& _decoderPool, QObject *parent)
    : QThread(parent), abort(_abort), decoderPool(_decoderPool), outputWriter(_decoderPool.getOutputWriter())
{
}

void DecoderThread::run()
{
    // Input and output data
    QVector<SourceField> inputFields;
    QVector<ComponentFrame> componentFrames;
    QVector<OutputFrame> outputFrames;

    while (!abort) {
        // Get the next batch of fields to process
        qint32 startFrameNumber, startIndex, endIndex;
        if (!decoderPool.getInputFrames(startFrameNumber, inputFields, startIndex, endIndex)) {
            // No more input frames -- exit
            break;
        }

        // Adjust the temporary arrays to the right size
        const qint32 numFrames = (endIndex - startIndex) / 2;
        componentFrames.resize(numFrames);
        outputFrames.resize(numFrames);

        // Decode the fields to component frames
        decodeFrames(inputFields, startIndex, endIndex, componentFrames);

        // Convert the component frames to the output format
        for (qint32 i = 0; i < numFrames; i++) {
            outputWriter.convert(componentFrames[i], outputFrames[i]);
            
            // Debug: Log first frame's RGB values (pixel 0 and pixel 1000)
            if (i == 0 && outputFrames[i].size() >= 3) {
                qInfo() << "DEBUG Decoder: Pixel 0: R=" << outputFrames[i][0]
                        << "G=" << outputFrames[i][1]
                        << "B=" << outputFrames[i][2];
            }
            if (i == 0 && outputFrames[i].size() >= 3003) {
                qInfo() << "DEBUG Decoder: Pixel 1000: R=" << outputFrames[i][3000]
                        << "G=" << outputFrames[i][3001]
                        << "B=" << outputFrames[i][3002];
            }
        }

        // Write the frames to the output file
        if (!decoderPool.putOutputFrames(startFrameNumber, outputFrames)) {
            abort = true;
            break;
        }
    }
}
