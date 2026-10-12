#!/bin/bash
# Build integration <sha7> into work/ab/m9c_<sha7> (Release, the integration tree's options, with the uncommitted
# FFX SDK + D3D12 Agility copied read-only from the integration tree so the build matches theirs). -j 2.
# usage: bash work/m5id/build.sh <sha7>
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
SHA=$1; B=work/ab/m9c_$SHA; O=work/m5id
TC="$PWD/.toolchain"; LM="$TC/llvm-mingw-20260922-ucrt-x86_64/bin"; CM="$TC/cmake-4.4.3-windows-x86_64/bin/cmake.exe"
INT="F:/Transformers Rebuild/Rebuild"
ls "F:/Transformers Rebuild/"*/work/PERF_HOLD 2>/dev/null && { echo "PERF_HOLD present: not building"; exit 2; }
if [ -x "$B/build-release/bin/wfc_rebuild.exe" ]; then echo "$SHA already built"; exit 0; fi
mkdir -p $B && git -C "$INT" archive $SHA | tar -x -C $B || exit 1
cp -r "$INT/third_party/ffx_sdk" "$INT/third_party/d3d12_agility" $B/third_party/ || exit 1
mkdir -p $B/work
[ -e $B/work/render ] || powershell -NoProfile -Command "New-Item -ItemType Junction -Path '$B/work/render' -Target 'F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m9c_37eec59\work\render'" > /dev/null
"$CM" -S $B -B $B/build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="$LM/clang.exe" -DCMAKE_CXX_COMPILER="$LM/clang++.exe" \
  -DCMAKE_RC_COMPILER="$LM/windres.exe" -DCMAKE_MAKE_PROGRAM="$TC/ninja/ninja.exe" -DWFC_BUILD_FIDELITY=ON -DWFC_DEV_TOOLS=ON \
  -DWFC_MIMALLOC=ON -DWFC_VGMSTREAM=ON -DWFC_LTO=OFF -DWFC_PGO="$PWD/$B/tools/pgo/wfc.profdata" > $O/cmake_$SHA.log 2>&1 || { echo "cmake failed"; tail $O/cmake_$SHA.log; exit 1; }
"$CM" --build $B/build-release --target wfc_rebuild -j 2 > $O/build_$SHA.log 2>&1 || { echo "build failed"; tail -20 $O/build_$SHA.log; exit 1; }
echo "built $SHA $(date +%H:%M:%S)"; ls $B/build-release/bin | grep -iE "fidelity|D3D12|wfc_rebuild"
