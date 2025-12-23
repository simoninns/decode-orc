#!/bin/bash

# Script to convert Qt types to standard C++ types
# Phase A: Data types and containers

set -e

echo "Converting Qt types to standard C++ types..."

# Files to convert (excluding lib/ and encoder/ subdirectories for now)
FILES="comb.cpp comb.h \
decoder.cpp decoder.h \
decoderpool.cpp decoderpool.h \
framecanvas.cpp framecanvas.h \
monodecoder.cpp monodecoder.h \
ntscdecoder.cpp ntscdecoder.h \
outputwriter.cpp outputwriter.h \
palcolour.cpp palcolour.h \
paldecoder.cpp paldecoder.h \
transformpal2d.cpp transformpal2d.h \
transformpal3d.cpp transformpal3d.h \
transformpal.cpp transformpal.h"

for file in $FILES; do
    if [ ! -f "$file" ]; then
        echo "Warning: $file not found, skipping..."
        continue
    fi
    
    echo "Processing $file..."
    
    # Create backup
    cp "$file" "$file.bak"
    
    # Convert Qt integer types
    sed -i 's/\bqint32\b/int32_t/g' "$file"
    sed -i 's/\bquint32\b/uint32_t/g' "$file"
    sed -i 's/\bqint16\b/int16_t/g' "$file"
    sed -i 's/\bquint16\b/uint16_t/g' "$file"
    sed -i 's/\bqint64\b/int64_t/g' "$file"
    sed -i 's/\bquint64\b/uint64_t/g' "$file"
    
    # Convert QVector to std::vector
    sed -i 's/\bQVector</std::vector</g' "$file"
    
    # Convert QString to std::string  
    sed -i 's/\bQString\b/std::string/g' "$file"
    
    # Convert QByteArray to std::vector<uint8_t>
    sed -i 's/\bQByteArray\b/std::vector<uint8_t>/g' "$file"
    
    # Add includes if needed (for header files)
    if [[ "$file" == *.h ]]; then
        # Check if <vector> is already included
        if ! grep -q '#include <vector>' "$file"; then
            # Add after other includes
            sed -i '/#include <QtGlobal>/a #include <vector>' "$file"
        fi
        if ! grep -q '#include <cstdint>' "$file"; then
            sed -i '/#include <QtGlobal>/a #include <cstdint>' "$file"
        fi
        if ! grep -q '#include <string>' "$file" && grep -q 'std::string' "$file"; then
            sed -i '/#include <QtGlobal>/a #include <string>' "$file"
        fi
    fi
    
    echo "  - Converted Qt integer types"
    echo "  - Converted QVector to std::vector"
    echo "  - Converted QString to std::string"
done

echo ""
echo "Phase A conversion complete!"
echo "Backup files created with .bak extension"
echo ""
echo "Next steps:"
echo "1. Manually review changes"
echo "2. Handle Qt-specific method calls (.fill, .squeeze, etc.)"
echo "3. Update includes (remove Qt headers, add C++ headers)"
echo "4. Test compilation"
