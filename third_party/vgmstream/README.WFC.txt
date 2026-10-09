vgmstream r2117 (libvgmstream), vendored for in-game decoding of the original FMOD sound banks (content/**/*.fsb, XMA2).

Source: https://github.com/vgmstream/vgmstream, tag r2117 (commit 71e2361042531fe767fb98300cf8c1ee95e539a0),
        archive refs/tags/r2117.tar.gz, sha256 b9ad0ffabb5919e1c9ec2912416e3ce37916417a8c410196822ce6c610dcde66.
        This is the same release that produced ExtractedAssets' WAVs (AssetTools bin/vgmstream/vgmstream-cli.exe r2117).
Kept:   src/**/*.c, *.h (unmodified); ffmpeg_include/ (= ext_includes/ffmpeg); ext_libs/*.def and ext_libs/dll-x64/ for the four
        FFmpeg DLLs only (avcodec-vgmstream-59, avformat-vgmstream-59, avutil-vgmstream-57, swresample-vgmstream-4; unmodified,
        md5-identical to the DLLs beside the r2117 vgmstream-cli); ffmpeg_build/ (vgmstream's FFmpeg build options / script /
        patch / BUILD-LIB.md, i.e. how those DLLs were built).
Built:  as a static library with the project's clang, FFmpeg codec only (VGM_USE_FFMPEG; no other external codec);
        the FFmpeg DLLs are linked through import libraries generated from the .def files and shipped next to the exe.

Licences (shipped with the package):
- vgmstream: COPYING (ISC-style permissive licence).
- FFmpeg (the four DLLs): LGPL, licenses/ffmpeg.COPYING.LGPLv2.1 and licenses/ffmpeg.COPYING.LGPLv3. They are used as separate,
  unmodified, replaceable DLLs. Corresponding source: FFmpeg as configured by ffmpeg_build/ (vgmstream's build of FFmpeg 5.x,
  libavcodec 59); FFmpeg source releases: https://ffmpeg.org/releases/ .

Bit-exactness (Systems gate): every bank's one-pass decode (ignore_loop, PCM16) must equal the PCM of the WAV beside it from
sample 0 for its full length (WAVs of loop-flagged banks may be longer: vgmstream-cli's default render adds 2 loops + a fade).
Tool: tools/systems/fsb_gate.cpp.
