# Fixed-cam verification (2026-10-07, 09c 5990311)

The legacy Streets perf cam `100,-700,-680,-141.6,-12` (used by every fixed-cam PERFORMANCE_LOG row before this date, and by
Rendering's A/Bs) sees a near-black close wall: `streets_legacy_cam.png` (luma ~13, 98 % flat; VISUALCHECK "near-black"). The
playable area from 6,584 bot positions is x 44..373, z -630..-321, ground y -707: the legacy cam is ~7 UU above ground and
outside the area (z -680). A/B deltas measured at it are like-for-like; absolute fps / GPU share are not representative.

`cam-sweep.ps1` tried 16 candidates (edge midpoints + corners of the area, +60 / +100 UU, aimed at the centre), scored
(luma, flatness, edge structure) and picked by eye from `streets_cams.png`:

**Streets overview: `44.3,-606.9,-475.7,-90.0,-31.3`** (c10, `streets_overview_c10.png`) - across the arena centre from the
west edge; lit, volumetric shaft, central structures. Not yet verified as the most-characters-on-screen view.

Debris / Molten: pending (sweeps queued; Debris direct boot needs the `_BASE_m` level name).
