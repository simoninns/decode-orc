#!/bin/bash

# Script to fix Qt-specific method calls and update includes
# Phase A continued: Method calls and includes

set -e

echo "Fixing Qt-specific method calls and includes..."

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
        continue
    fi
    
    echo "Fixing includes in $file..."
    
    # Remove Qt includes (except ones still needed)
    sed -i '/#include <QtGlobal>/d' "$file"
    sed -i '/#include <QVector>/d' "$file"
    sed -i '/#include <QString>/d' "$file"
    sed -i '/#include <QByteArray>/d' "$file"
    
    # For .cpp files, add algorithm if they use fill
    if [[ "$file" == *.cpp ]] && grep -q 'std::fill\|\.fill' "$file"; then
        if ! grep -q '#include <algorithm>' "$file"; then
            # Add after first include
            sed -i '1a #include <algorithm>' "$file"
        fi
    fi
done

echo "Done fixing includes!"
