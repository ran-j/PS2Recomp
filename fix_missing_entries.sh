#!/bin/bash
# Iteratively add missing entry points until no more are needed

cd /c/Users/User/Documents/decompilations/sly1-ps2-decomp/tools/ps2recomp

for i in {1..50}; do
    echo "=== Iteration $i ==="

    # Run game and capture missing address
    OUTPUT=$(timeout 15 ./build/ps2xRuntime/ps2EntryRunner.exe "C:/Users/User/Documents/decompilations/sly1-ps2-decomp/disc/SCUS_971.98" 2>&1)

    MISSING=$(echo "$OUTPUT" | grep -oP "No func at 0x[0-9a-f]+" | head -1 | grep -oP "0x[0-9a-f]+")
    CALLS=$(echo "$OUTPUT" | grep -oP "Total calls: \d+" | grep -oP "\d+")

    echo "Total calls: $CALLS"

    if [ -z "$MISSING" ]; then
        echo "No more missing functions!"
        break
    fi

    echo "Missing: $MISSING"

    # Add entry point
    python3 add_entry.py $MISSING

    # Regenerate
    ./build/ps2xRecomp/ps2recomp.exe sly1_config.toml 2>&1 | tail -1

    # Copy files
    cp ../../recomp_output/ps2_recompiled_functions.cpp ps2xRuntime/src/runner/
    cp ../../recomp_output/register_functions.cpp ps2xRuntime/src/runner/
    cp ../../recomp_output/ps2_recompiled_functions.h ps2xRuntime/include/
    cp ../../recomp_output/ps2_recompiled_stubs.h ps2xRuntime/include/

    # Rebuild
    cd build
    cmake --build . --config Release 2>&1 | tail -3
    cd ..

    echo ""
done
