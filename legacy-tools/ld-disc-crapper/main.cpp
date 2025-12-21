/************************************************************************

    main.cpp

    ld-disc-crapper - Generate broken TBC files for disc mapper testing
    Copyright (C) 2025 Simon Inns

    This file is part of decode-orc legacy tools.
    
    Based on the ld-decode TBC library.

    This is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

************************************************************************/

#include <QCoreApplication>
#include <QDebug>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>

#include "sourcevideo.h"
#include "lddecodemetadata.h"
#include "logging.h"

#include <vector>
#include <random>
#include <algorithm>

// Track corruption events for reporting
struct CorruptionEvent {
    enum Type { SKIP, REPEAT, GAP };
    Type type;
    qint32 startField;
    qint32 endField;
    qint32 count;
};

// Corruption pattern definitions
struct CorruptionPattern {
    QString name;
    QString description;
    int skipFields;
    int repeatFields;
    int gapSize;
    double corruptionRate;
};

static std::vector<CorruptionPattern> getPatterns() {
    return {
        {"simple-skip", "Skip 5 fields every 100 fields", 5, 0, 0, 0.01},
        {"simple-repeat", "Repeat 3 fields every 50 fields", 0, 3, 0, 0.02},
        {"skip-with-gap", "Skip 10 fields and insert 5 black fields every 200 fields", 10, 0, 5, 0.005},
        {"heavy-skip", "Skip 15 fields every 100 fields (severe damage)", 15, 0, 0, 0.01},
        {"heavy-repeat", "Repeat 5 fields every 30 fields (severe sticking)", 0, 5, 0, 0.033},
        {"mixed-light", "Light mix of skips and repeats", 3, 2, 0, 0.02},
        {"mixed-heavy", "Heavy mix of skips, repeats, and gaps", 10, 5, 3, 0.05}
    };
}

class TBCCorruptor {
public:
    TBCCorruptor(const QString& inputFile, const QString& outputFile, const CorruptionPattern& pattern)
        : inputFile_(inputFile), outputFile_(outputFile), pattern_(pattern), rng_(std::random_device{}()) {}
    
    bool process() {
        qInfo() << "=== ld-disc-crapper ===";
        qInfo() << "Input: " << inputFile_;
        qInfo() << "Output:" << outputFile_;
        qInfo() << "Pattern:" << pattern_.name;
        
        // Open input TBC
        SourceVideo sourceVideo;
        LdDecodeMetaData metaData;
        
        // Load metadata first to get proper field dimensions
        QString metadataFile = inputFile_ + ".db";
        if (!metaData.read(metadataFile)) {
            qCritical() << "Failed to read metadata:" << metadataFile;
            return false;
        }
        
        qint32 totalFields = metaData.getNumberOfFields();
        LdDecodeMetaData::VideoParameters videoParams = metaData.getVideoParameters();
        
        // Open video with correct dimensions
        if (!sourceVideo.open(inputFile_, videoParams.fieldWidth * videoParams.fieldHeight, videoParams.fieldWidth)) {
            qCritical() << "Failed to open input TBC:" << inputFile_;
            return false;
        }
        qInfo() << "Input TBC:" << totalFields << "fields," << videoParams.fieldWidth << "x" << videoParams.fieldHeight << "samples";
        
        // Generate corruption mapping
        std::vector<qint32> fieldMapping = generateFieldMapping(totalFields);
        
        qInfo() << "Output will have" << fieldMapping.size() << "fields";
        
        // Create output TBC
        QFile outputTbc(outputFile_);
        if (!outputTbc.open(QIODevice::WriteOnly)) {
            qCritical() << "Failed to create output TBC:" << outputFile_;
            return false;
        }
        
        // Process fields
        qint32 outputField = 0;
        qint32 normalCount = 0;
        qint32 repeatCount = 0;
        qint32 skipCount = 0;
        qint32 gapCount = 0;
        qint32 lastSourceField = -1;
        
        std::vector<CorruptionEvent> corruptionEvents;
        
        qint32 fieldLength = sourceVideo.getFieldLength();
        
        for (qint32 sourceField : fieldMapping) {
            if (sourceField < 0) {
                // Gap - write black field
                writeBlackField(outputTbc, fieldLength);
                gapCount++;
                qDebug() << "Field" << outputField << ": GAP (black)";
            } else if (sourceField < 1 || sourceField > totalFields) {
                qCritical() << "ERROR: Field" << sourceField << "out of range [1.." << totalFields << "]";
                return false;
            } else {
                // Read and write real field
                SourceVideo::Data fieldData = sourceVideo.getVideoField(sourceField);
                if (fieldData.isEmpty()) {
                    qCritical() << "Failed to read field" << sourceField;
                    return false;
                }
                
                // Write field as bytes
                qint64 bytesWritten = outputTbc.write(reinterpret_cast<const char*>(fieldData.data()), 
                                                      fieldData.size() * sizeof(quint16));
                
                if (bytesWritten != fieldData.size() * sizeof(quint16)) {
                    qCritical() << "Failed to write field" << outputField;
                    return false;
                }
                
                // Update statistics and record corruption events
                if (lastSourceField >= 0) {
                    if (sourceField == lastSourceField) {
                        repeatCount++;
                        corruptionEvents.push_back({CorruptionEvent::REPEAT, sourceField, sourceField, 1});
                        qDebug() << "Field" << outputField << ": REPEAT field" << sourceField;
                    } else if (sourceField > lastSourceField + 1) {
                        qint32 skipped = sourceField - lastSourceField - 1;
                        skipCount += skipped;
                        corruptionEvents.push_back({CorruptionEvent::SKIP, lastSourceField + 1, sourceField - 1, skipped});
                        qDebug() << "Field" << outputField << ": from field" << sourceField 
                                 << "(skipped" << skipped << ")";
                    } else {
                        normalCount++;
                    }
                } else {
                    normalCount++;
                }
                
                lastSourceField = sourceField;
                
                // Copy metadata for this field
                // Note: VBI frame numbers will be wrong for skipped/repeated fields
                // This is intentional - it simulates the broken state we want to fix
            }
            
            outputField++;
        }
        
        outputTbc.close();
        
        // Create new metadata with only the fields we wrote
        // Start with a copy of video parameters
        LdDecodeMetaData outputMetaData;
        outputMetaData.setVideoParameters(metaData.getVideoParameters());
        
        // Copy field metadata for each output field
        // Note: numberOfSequentialFields and seqNo will be automatically set by appendField()
        // Field metadata (including VBI) is copied from the actual source field being written.
        // For repeated fields, the same source metadata is used multiple times.
        // For skipped fields, no metadata entry is created (simulating missing fields).
        for (size_t i = 0; i < fieldMapping.size(); ++i) {
            qint32 sourceFieldNum = fieldMapping[i];
            
            if (sourceFieldNum > 0) {
                // Copy all metadata from the source field being written (fields are 1-indexed)
                // This includes VBI data, drop-outs, etc. from the actual field
                LdDecodeMetaData::Field fieldMeta = metaData.getField(sourceFieldNum);
                
                // appendField() will automatically set seqNo to maintain sequential numbering
                outputMetaData.appendField(fieldMeta);
            } else {
                // Gap field (black field) - create minimal metadata
                LdDecodeMetaData::Field fieldMeta;
                fieldMeta.isFirstField = (i % 2 == 0);
                fieldMeta.syncConf = 0;
                fieldMeta.pad = true;  // Mark as padding/gap field
                
                // appendField() will automatically set seqNo to maintain sequential numbering
                outputMetaData.appendField(fieldMeta);
            }
        }
        
        // Write output metadata (delete old file first to ensure clean write)
        QString outputMetadataFile = outputFile_ + ".db";
        QFile::remove(outputMetadataFile);  // Delete old metadata file if it exists
        if (!outputMetaData.write(outputMetadataFile)) {
            qCritical() << "Failed to write output metadata:" << outputMetadataFile;
            return false;
        }
        
        // Print statistics
        qInfo() << "";
        qInfo() << "=== Statistics ===";
        qInfo() << "  Normal fields:  " << normalCount;
        qInfo() << "  Repeated fields:" << repeatCount;
        qInfo() << "  Skipped fields: " << skipCount;
        qInfo() << "  Gap fields:     " << gapCount;
        qInfo() << "  Total output:   " << fieldMapping.size();
        
        // Print corruption details
        if (!corruptionEvents.empty()) {
            qInfo() << "";
            qInfo() << "=== Corruption Details ===";
            qInfo() << "(Frame numbers shown - visible in ld-analyse VBI display)";
            qInfo() << "";
            
            for (const auto& event : corruptionEvents) {
                qint32 startFrame = (event.startField + 1) / 2;  // Convert field to frame
                qint32 endFrame = (event.endField + 1) / 2;
                
                switch (event.type) {
                case CorruptionEvent::SKIP:
                    if (startFrame == endFrame) {
                        qInfo() << "  SKIP: Frame" << startFrame << "(" << event.count << "field" << (event.count > 1 ? "s)" : ")");
                    } else {
                        qInfo() << "  SKIP: Frames" << startFrame << "-" << endFrame << "(" << event.count << "fields)";
                    }
                    break;
                case CorruptionEvent::REPEAT:
                    qInfo() << "  REPEAT: Frame" << startFrame << "(field" << event.startField << ")";
                    break;
                case CorruptionEvent::GAP:
                    qInfo() << "  GAP: " << event.count << "black field" << (event.count > 1 ? "s" : "");
                    break;
                }
            }
        }
        
        qInfo() << "";
        qInfo() << "Corruption complete!";
        
        return true;
    }

private:
    std::vector<qint32> generateFieldMapping(qint32 totalFields) {
        std::vector<qint32> mapping;
        
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        
        // Fields are 1-indexed in SourceVideo
        for (qint32 i = 1; i <= totalFields; ++i) {
            // Check if we should corrupt this field
            bool shouldCorrupt = (dist(rng_) < pattern_.corruptionRate);
            
            if (shouldCorrupt) {
                if (pattern_.skipFields > 0 && i + pattern_.skipFields <= totalFields) {
                    // Skip fields - just don't add them to mapping, advance i
                    qDebug() << "Skipping fields" << i << "to" << (i + pattern_.skipFields - 1);
                    i += pattern_.skipFields - 1;  // -1 because loop will increment
                    continue;
                } else if (pattern_.repeatFields > 0) {
                    // Repeat this field multiple times
                    qDebug() << "Repeating field" << i << "x" << pattern_.repeatFields;
                    for (int r = 0; r < pattern_.repeatFields; ++r) {
                        mapping.push_back(i);
                    }
                    continue;  // Don't add normal field below
                } else if (pattern_.gapSize > 0) {
                    // Insert gap (black fields)
                    qDebug() << "Inserting" << pattern_.gapSize << "black fields at" << i;
                    for (int g = 0; g < pattern_.gapSize; ++g) {
                        mapping.push_back(-1);
                    }
                    // Don't continue - still add the normal field
                }
            }
            
            // Add normal field
            mapping.push_back(i);
        }
        
        return mapping;
    }
    
    void writeBlackField(QFile& file, qint32 samples) {
        QByteArray blackField(samples * 2, 0);  // 16-bit samples = 2 bytes each
        file.write(blackField);
    }

private:
    QString inputFile_;
    QString outputFile_;
    CorruptionPattern pattern_;
    std::mt19937 rng_;
};

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    
    // Install debug handler
    setDebug(true);
    qInstallMessageHandler(debugOutputHandler);
    
    QCoreApplication::setApplicationName("tbc-corruption-tool");
    QCoreApplication::setApplicationVersion("1.0.0");
    
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "ld-disc-crapper - Generate broken TBC files for disc mapper testing\n"
        "\n"
        "This tool reads a clean TBC file and creates a corrupted version with\n"
        "skipped fields, repeated fields, and gaps to simulate laserdisc player\n"
        "tracking problems. Used for testing the disc mapper functionality.\n"
        "\n"
        "Based on the ld-decode TBC library.\n"
        "\n"
        "(c)2025 Simon Inns\n"
        "GPLv3 Open-Source - Part of decode-orc project");
    
    parser.addHelpOption();
    parser.addVersionOption();
    
    addStandardDebugOptions(parser);
    
    // List patterns option
    QCommandLineOption listPatternsOption(QStringList() << "list-patterns",
                                         "List available corruption patterns");
    parser.addOption(listPatternsOption);
    
    // Pattern option
    QCommandLineOption patternOption(QStringList() << "p" << "pattern",
                                    "Corruption pattern to apply",
                                    "pattern-name");
    parser.addOption(patternOption);
    
    // Positional arguments
    parser.addPositionalArgument("input", "Input TBC file");
    parser.addPositionalArgument("output", "Output TBC file");
    
    parser.process(a);
    
    // List patterns if requested
    if (parser.isSet(listPatternsOption)) {
        qInfo() << "Available corruption patterns:\n";
        for (const auto& p : getPatterns()) {
            qInfo().noquote() << "  " << p.name;
            qInfo().noquote() << "    " << p.description;
            qInfo() << "";
        }
        return 0;
    }
    
    // Check arguments
    QStringList positionalArgs = parser.positionalArguments();
    if (positionalArgs.size() != 2) {
        qCritical() << "Error: Requires input and output file arguments";
        parser.showHelp(1);
    }
    
    if (!parser.isSet(patternOption)) {
        qCritical() << "Error: Pattern must be specified with -p/--pattern";
        qCritical() << "Use --list-patterns to see available patterns";
        return 1;
    }
    
    QString inputFile = positionalArgs[0];
    QString outputFile = positionalArgs[1];
    QString patternName = parser.value(patternOption);
    
    // Find pattern
    auto patterns = getPatterns();
    auto it = std::find_if(patterns.begin(), patterns.end(),
                          [&](const CorruptionPattern& p) {
                              return p.name == patternName;
                          });
    
    if (it == patterns.end()) {
        qCritical() << "Error: Unknown pattern" << patternName;
        qCritical() << "Use --list-patterns to see available patterns";
        return 1;
    }
    
    // Run corruption
    TBCCorruptor corruptor(inputFile, outputFile, *it);
    
    if (!corruptor.process()) {
        qCritical() << "Corruption failed!";
        return 1;
    }
    
    return 0;
}
