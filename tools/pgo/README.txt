wfc.profdata: PGO profile for -DWFC_PGO=<repo>/tools/pgo/wfc.profdata (Release).
Regenerate: build with -DWFC_PGO=gen, then BUILD=<that build dir> LLVM_PROFDATA=<llvm-profdata> bash tools/systems/pgo_train.sh (from the repo root).
Trained on 09c 2487560 (final head for the playtest build): 6 live TDM matches (Streets / Rust / Molten x 19 / 63 bots, 90 s each, + the frontend flow, seeded).
A/B on 17275ac: sim step -2..-6 %, 64p fps +1..+3.5 %, p99 -4.5 % (player cam).
