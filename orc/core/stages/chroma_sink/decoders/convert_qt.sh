#!/bin/bash
# Qt6 to C++ conversion script

cd /home/sdi/Coding/github/decode-orc/orc/core/stages/chroma_sink/decoders

# Convert all .h and .cpp files (excluding lib/tbc for now - we'll minimize that separately)
find . -maxdepth 1 -name "*.h" -o -maxdepth 1 -name "*.cpp" | while read file; do
    echo "Converting $file..."
    
    # Integer types
    sed -i 's/\bqint32\b/int32_t/g' "$file"
    sed -i 's/\bquint32\b/uint32_t/g' "$file"
    sed -i 's/\bqint16\b/int16_t/g' "$file"
    sed -i 's/\bquint16\b/uint16_t/g' "$file"
    sed -i 's/\bqint64\b/int64_t/g' "$file"
    sed -i 's/\bquint64\b/uint64_t/g' "$file"
    
    # Container types
    sed -i 's/QVector</std::vector</g' "$file"
    sed -i 's/QString /std::string /g' "$file"
    sed -i 's/QString&/std::string\&/g' "$file"
    sed -i 's/(QString)/(std::string)/g' "$file"
    
    # Metadata types
    sed -i 's/LdDecodeMetaData::VideoParameters/VideoParameters/g' "$file"
    sed -i 's/#include "lddecodemetadata.h"/#include "video_parameters.h"/g' "$file"
    
    # QVector methods
    sed -i 's/\.fill(/std::fill(/g' "$file"
    sed -i 's/\.squeeze()/.shrink_to_fit()/g' "$file"
    
    # Threading
    sed -i 's/QThread::/std::/g' "$file"
    sed -i 's/QAtomicInt/std::atomic<int>/g' "$file"
    sed -i 's/QMutex/std::mutex/g' "$file"
    
    # Includes
    sed -i 's/#include <QtGlobal>/#include <cstdint>/g' "$file"
    sed -i 's/#include <QVector>/#include <vector>/g' "$file"
    sed -i 's/#include <QString>/#include <string>/g' "$file"
    sed -i 's/#include <QThread>/#include <thread>/g' "$file"
    sed -i 's/#include <QAtomicInt>/#include <atomic>/g' "$file"
    sed -i 's/#include <QMutex>/#include <mutex>/g' "$file"
    sed -i 's/#include <QDebug>\/\/ Removed QDebug/g' "$file"
    sed -i 's/#include <QObject>//g' "$file"
done

echo "Basic conversions complete"
