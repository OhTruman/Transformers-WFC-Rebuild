# SYSTEMS_HD_PBR_PACKAGE (against integration b85227e7): ship the M6 roughness maps for HD Enhanced

What the runtime reads (WfcPipeline.cpp hdPbrIndex, b85227e7): hd_pbr_index.json (else hd_modern_index.json) in the SAME HD
root as hd_index.json (hdRoot(): the package default Transformers Rebuild/AssetTools/out/hd, or WFC_HD_ROOT), dds paths
relative to that root; rows tex / role / dds / format BC4 | BC7 / space linear / bytes; a bad row is skipped (that material
derives its roughness), never the pack. Read only at HD level 2 (Enhanced = HD textures + Modern materials). There is no
separate WFC_HD_PBR_ROOT. Logs "wfc: HD modern material maps: <n> (hd_pbr_index.json)[ - <k> rows skipped ...]".

What package 5 / build_slim did: shipped the D pack only (hd_index.json + its DDS); no hd_pbr_index.json, no m6/ -> HD
Enhanced had Modern materials (materials_pbr.json) but every roughness map derived. verify_slim's HD runtime row forced
level 1 (Faithful), so Enhanced never ran in a gate; and its unindexed-files rule would have flagged the m6/ files.

Patch:
- build_slim.py --hd-pbr <pack> (e.g. AssetTools/out/hd/push_m6/pack): hd_pbr_index.json + only the indexed DDS into the
  same HD root; every row checked as the loader does plus byte + sha256 against FROZEN.json (pack folder or its parent);
  any failing row stops the build; needs --hd; build_slim.json gains hd_pbr {source, name, rows, files, frozen}.
- verify_slim.ps1: static - hd_pbr_index.json rows checked (tex / role / dds, BC4 | BC7, linear, file, bytes), its files
  count as indexed; the HD line reports "<rows> rows + <n> M6 modern material rows". Runtime - -HdLevel (1 | 2) for
  -HdForced; the HD runtime row runs at level 2 when the package ships the M6 index and then requires "HD modern material
  maps: <all rows> (hd_pbr_index.json)" with nothing skipped and "Modern materials on for MP_<map>".

Tests (64771c71 dry-run package, an HD root of D content + push_m6):
- build_slim --plan: "HD M6 modern material maps: 498 of 498 rows (FROZEN.json push_m6_20261016: bytes + sha256 checked)",
  HD 4,005 files / 21.70 GB.
- verify_slim -StaticOnly: PASS (3,505 rows + 498 M6 rows, 0 problems); one M6 file removed: FAIL naming it.
- verify_slim Streets -HdForced -HdLevel 2 (RT off): PASS - "HD texture pack available: 3505", "HD modern material maps:
  498 (hd_pbr_index.json)", "Modern materials on for MP_IAC_Streets (537 pbr bodies)".

Refresh command for package 6: refresh.sh's build_slim line gains --hd-pbr "F:/Transformers Rebuild/AssetTools/out/hd/push_m6/pack".
