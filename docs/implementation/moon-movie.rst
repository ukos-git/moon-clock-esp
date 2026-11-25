The boot movie
==============

The boot presentation's first source (user decision 2026-11-25): a
**precomputed synodic cycle of the moon phases**, baked once at build
time by the portable C core, stored **flash-resident** in the
``moon_movie`` data partition, and played **looping** on the panel
while the live current phase bakes in parallel. When the live bake
lands, the movie plays forward to the frame matching the current
phase — and the computed frame reveals over it, atomically.

The movie exists because the pre-movie boot had a cold gap: the card
replay (2026-11-14) played *stored* hour frames, so a first boot —
empty card, wiped folder, or no card at all — had nothing to show and
sat on the quick phase disc (or black, pre-2026-11-24) until the live
bake landed. A movie baked at build time is present on *every* boot:
no SD card, no probe, no cold folder. The TF card keeps its job — the
**hourly refresh cache** only (:doc:`the animation pipelines
<animations>`) — and the boot's movie path **never touches the SD
bus** (the ~1 s mount probe now lives only on the card-fallback leg,
below).

.. figure:: ../_static/figs/perf_movie.png
   :alt: the boot movie — per-board footprint against the moon_movie row, and the frames over the synodic cycle
   :figclass: align-center

   The movie, priced. **Left:** each board's baked blob against its
   ``moon_movie`` row (the white bar is the row — the ceiling; the
   generator fills it with the finest phase step that fits — the "fill
   the space, never hinder the code build" policy). **Right:** the
   frames over the cycle — one strip per board over the 709 h from the
   header's ``ref_center``, every tick one baked frame; the density is
   what adapts to the row (S3 and knob at 355 × 2 h, P4 at 178 × 4 h),
   not the playback rate — and the loop's last step is one hour of
   phase (frame n−1 at 708 h wraps to frame 0 ≡ 709 h), so the loop is
   seamless by construction. The time paths against the 240 s budget
   are :doc:`the boot figure <../verification/performance>`. The
   generator is ``docs/tools/gen_perf_movie.py`` (manifests + the
   committed partition rows + the on-disk blob sizes asserted equal —
   parsed at figure time, so figure and sources cannot drift apart).

What is in the movie
--------------------

One **synodic cycle** per board: one frame per
``hours_per_frame`` hour centre spanning 709 h (29.53 d — the same
span the hour cache caps at, ``TF_MAX_HOURS``), each frame a q90
**grayscale** JPEG of a *neutral* bake — event-independent, exactly
like the stored TF-cache grays (classifying ~700 epochs would cost
more than the animation is worth; the final reveal carries the event
tint). Each frame is a RAW bake of the **portable C core** (the
``moon_sim`` path with the board's own ``--radius`` geometry,
``dark_zero``, neutral LUT) — **pixel-identical to a device bake**,
the same provenance rule as the on-disk textures
(:doc:`the C core <core-c>`). Frame *i*'s hour centre is
``ref_center + i * hours_per_frame * 3600`` (epoch seconds, UTC), and
``ref_center`` is a fixed constant baked into the header — 1767226800
(2026-01-01T01:00Z) today — so a movie's phase-to-index mapping is
identical across firmware versions with the same calcver.

Because the span is one synodic month, **frame 0 ≈ frame n−1**: the
loop wraps with at most a half-step of phase (the seam is a sub-frame
jump — invisible at the panel's drain rate), so the loop is seamless
by construction. The *density* of the cycle — how many frames fill it
— is what adapts to the flash budget, never the playback rate:
playback is panel-capped (~6 fps S3) and independent of how many
frames the movie holds.

The container
-------------

``assets/images/moon/moon/movie.py`` (``.venv/bin/python -m moon.movie
--target s3 p4 knob``) bakes one blob per board under
``assets/images/moon/out/<target>/`` (gitignored, like the other
asset outputs — :doc:`the Python API <../reference/python-api>`):
``moon_movie.bin`` + ``moon_movie_manifest.json``. The bin is a
62-byte little-endian header (magic ``MOOV``, the **calcver**, the
target, ``s`` = the board's ``MOON_PX``, the disk radius, ``q90``,
``hours_per_frame``, ``frame_count``, ``ref_center``, and the table /
frame offsets), then a ``frame_count`` × u32 offset table (offsets
relative to the first frame), then the q90 gray JPEGs packed back to
back. The manifest carries the same facts plus the measured sizes —
the single source the docs figures and the CMake guards re-derive
from.

The generator is **size-adaptive**: it reads the committed
``moon_movie`` partition row's size (the row is the source of truth —
the partition CSVs are committed, static files), tries the finest
phase step first (1 h/frame = 709 frames), and steps coarser (2 h, 4
h, …) until the measured JPEG total fits the row. It needs only the
cached ``moon-kit/`` — no network. If **zero** frames fit it still
emits a valid zero-frame blob, so a movie never blocks a flash: a
zero-frame movie reads as *no movie* and the presentation falls back
(the ladder below).

Flash residency (all three tables)
----------------------------------

Every table grew a ``moon_movie`` row, sized to the chip's leftover
after the other rows (the single-sourced tables:
:doc:`the partitions <../reference/partitions>`):

- **S3** — ``11648 K``, the LAST row (after ``moon_assets``). 355
  frames @ 2 h = 10,264,284 B today.
- **P4** — ``18304 K``, the LAST row — the row's start sits **past
  the old 16 MB mark**, which is what the wave's 32 MB header bump
  exists for (:doc:`the binary-size chronology <../verification/binary>`,
  the P4's 32 MB header). 178 frames @ 4 h = 12,842,691 B today.
- **Knob** — ``9088 K``, the LAST-but-one row, with the 64 K
  ``coredump`` witness ring after it (consumers find every partition
  by label, so the ring's offset move was free). The knob has **no SD
  socket — flash is its ONLY movie source**. 355 frames @ 2 h =
  6,075,946 B today.

The **sizing policy** ("fill the space, never hinder the code build"):
the app is bounded by its own ``factory`` row (S3 2048 K / P4 9216 K /
knob 4096 K), so **the movie can never crowd the code** — a flash
always succeeds. If the code grows later, the fix is a
``partitions.csv`` edit: widen ``factory``, shrink ``moon_movie``,
re-run the generator — and it lands on a coarser frame step
automatically. That *is* the "dynamically decrease the phase-frame
density" decision, and it is a generator property, not a build-time
resize.

The build chain
---------------

One ``idf.py flash`` writes the app **and** the movie (the storage
SPIFFS and the ``moon_assets`` blob too): the root ``CMakeLists.txt``
and the knob's own add ``esptool_py_flash_to_partition(flash
"moon_movie" ...)``, mirroring the ``moon_assets`` rule. Three
configure-time guards run before the app compiles (the knob's
``CMakeLists.txt`` replicates the trio):

1. **The whole partition table fits the flash** — the row sizes sum
   to at most ``CONFIG_ESPTOOLPY_FLASHSIZE`` (16 MB S3 / 32 MB P4 /
   16 MB knob): the "never hinder the code build" gate.
2. **The blob fits its row** — ``moon_movie.bin`` ≤ the
   ``moon_movie`` row (a belt for the generator; a resize mistake
   fails fast with the KiB named).
3. **The manifest matches the firmware** — the manifest's target and
   its **calcver** equal the version the firmware is stamped with:
   the root ``CMakeLists`` derives ``MOON_CALC_VER`` from the
   commitizen ``.cz.toml`` ``version`` line (git ``describe``
   fallback) into ``-DMOON_CALC_VER`` on every compile, and the knob
   replicates the derive against ``../.cz.toml``. A blob baked for
   another board or an older core is a build-time ``FATAL_ERROR``, not
   a boot-time surprise.

A *missing* blob is only a warning (the movie degrades to the card /
disc path, and the code build flashes) — a malformed manifest or a
non-fitting blob is not.

The reader
----------

``components/moon/esp/moon_esp_movie.c`` (shared by all three
boards; the knob links it through ``EXTRA_COMPONENT_DIRS`` like the
other shared components): ``moon_movie_init()`` finds the partition
(``esp_partition_find_first(DATA, UNDEFINED, "moon_movie")``), reads
the 62-byte header and validates the magic **and the calcver** (a
mismatch reads as *no movie* — and a stale movie implies a stale card
folder, since the TF cache's folder is versioned by the same calcver,
so the fallback lands on the same disc/black floor the stale folder
already gives). ``moon_movie_fetch(idx, gray_out, s_px)`` reads one
frame's JPEG and decodes it through the **same ``esp_new_jpeg`` codec
path the TF hour cache runs** (RGB888 out, take R — a gray JPEG
decodes with R == G == B) into the caller's ``s_px``-square gray
bounce; the transient decode scratch (``s_px``²·3, PSRAM, lazy) is the
same shape ``tfcache_fetch`` already allocated. Single-threaded caller
contract, no locking — the same rule as tfcache (the boot producer is
the sole caller). Runtime RAM cost is one more PSRAM bounce per
board; **internal RAM is untouched**, and the partition is read-only
at runtime (no flash wear).

The clocks' presentation (``UI_MOON_BOOT_REPLAY``)
---------------------------------------------------

The gate stays ``UI_MOON_BOOT_REPLAY`` (default y — the movie is the
new first source under it; one gate, not a second). Two core-1 tasks
(12 KB internal-RAM stacks, ``configMAX_PRIORITIES − 3``, spawned by
``moon_ui_start`` in that order — :doc:`the architecture
<architecture>`):

**The parallel bake (``moon_bake``)** — spawned FIRST so a failed
spawn degrades the presentation straight to the stale-frame re-bake.
It owns the live leg: if the clock holds no trustworthy instant yet
(RTC unset, NTP not synced) it polls at 500 ms — the movie keeps
looping meanwhile — and once the clock syncs it classifies the day's
sky event, bakes the current phase into the shared ``s_hourly_gray``
bounce with the compute-progress watermark (``COMPUTE_REPLAY_FINAL`` —
the seconds arc sweeps its rows-completed bar, exactly like the
hourly leg; :doc:`the compute progress <compute-progress>`), and
sets ``s_boot_bake_done`` (or ``s_boot_bake_failed`` on abort / no
time / no albedo). The strip loop yields per strip (``vTaskDelay(1)``
inside ``moon_bake_progress``), so the same-priority, same-core
producer keeps feeding movie frames while the bake runs. It **never**
touches ``s_frame`` / ``s_rgb`` / the panel — the producer owns the
publish, and the two tasks never share a buffer (the bake owns
``s_hourly_gray``; the producer plays from its own fetch bounce).

**The producer (``moon_replay``)** — plays the **source ladder**:

1. **The flash movie** (``moon_movie_init`` valid: magic + calcver +
   ``s`` + at least one frame): the frames play **LOOPING** — one
   frame per tick, wrapping, neutral LUT (``moon_gray565_set_event``
   off — the frames are event-independent), each publish in ONE
   ``s_rgb_mtx`` block, each show landed by the tick as ONE atomic
   whole-panel ``lv_refr_now`` under ``s_rgb_mtx``, drain-witnessed
   (the family invariant; the movie adds **no new exception** — its
   reads happen off-panel on core 1 and its shows ride the
   sanctioned atomic-show path). The loop is **unbounded** — there is
   no stop while it loops: the loop IS the animation, and the bake
   completing is what ends it. (A *repeating* fetch failure — 100
   consecutive misses, a torn partition or flash error — stops the
   loop and degrades to the final leg; a healed single miss is
   skipped.)
2. **The card fallback** (no partition / calcver mismatch /
   zero-frame movie): the 2026-11-14 card replay verbatim — the lazy
   mount happens on THIS leg's first moment (a card-less fallback pays
   the ~1 s probe here; the movie path never touches the bus at
   boot), the stored hours play ascending, neutral tint, the
   ``UI_MOON_REPLAY_BUDGET_MS`` budget bounding that leg.
3. **The floor**: the quick phase disc ``moon_ui_start`` painted
   before the spawn (or black without a trustworthy time) stays up
   under both.

**Catch-up and reveal.** When ``s_boot_bake_done`` flips, the
producer maps the now hour to the **nearest** loop index by hour
arithmetic on the header's ``ref_center`` — no new-moon scan: frame
*i*'s centre is ``ref + i·hpf`` hours, so the nearest frame is within
``hpf/2`` hours of the now-frame's phase and the swap over it is
seamless. Far targets (more than ``n/4`` of the cycle ahead,
``REPLAY_CATCHUP_QUARTER``) take **one settle frame at the target
then the reveal** (a hard cut — it's a loading screen); near targets
**play forward one frame per tick** to the current phase,
budget-bounded. Then the **final leg** — common to both sources: a
bounded wait if the bake is still running (the budget cuts the
*animation*, never the moon — an overrunning wait degrades to the
stale-frame re-bake backstop), a bounded handshake that the last
published frame has *landed* before the now-frame replaces it
("play to the current phase, **then** reveal the computed image"),
the now-frame published under ``s_rgb_mtx`` with the bake's **event
tint** (the producer is the presentation's sole LUT writer), the
bookkeeping committed there and only there (``s_orbit_epoch`` /
``s_baked_hour`` / ``s_baked_event`` / ``s_baked_day``), and the tick
lands it as the last atomic show (drain-witnessed as
``boot replay final``). The card's new-moon prune (the ~8 s
``new_moon_epoch`` scan) still rides the final leg. Movie frames never
move ``s_orbit_epoch`` — they are historic phases; only the final
bake may.

**Pacing and the budget.** The producer paces at the floor
``1000 / UI_MOON_REPLAY_FPS`` (default 8 → 125 ms/frame minimum
dwell); the actual rate is what the 50 ms tick and the whole-panel
flush allow on top (~6 fps measured S3 — :doc:`the performance
comparison <../verification/performance>`). ``UI_MOON_REPLAY_BUDGET_MS``
(240 s default) now bounds the **bounded legs only** — the
card-fallback leg and the catch-up leg. The movie **loop itself is
unbounded**: its stopper is the bake, and the bake's stopper is the
clock sync.

**Arbitration and abort.** While ``moon_ui_replaying()`` is true the
hourly probe/spawn, the day-tick and the orbit gate out and every
idle re-arm site defers — the three core-1 producers (replay,
parallel bake, hourly) never run at once, which is what keeps
tfcache's single-caller contract intact. A sleep entry or a screen
switch away from the moon raises ``s_replay_abort``: the un-shown
frames are dropped, the final publish is skipped, ``s_baked_day``
stays 0 — the minute day-tick queues the corrective re-bake on
return — and ``s_frame_stale`` marks a degraded run (the panel rests
on the last COMPLETE frame, a movie frame or the disc; nothing to
heal). Demo mode boots without the presentation (its scripted tour
owns the display); ``UI_MOON_BOOT_REPLAY=n`` boots straight to the
static current phase, touching no SD bus.

The knob (``KNOB_UI_MOVIE_ENABLE``)
-----------------------------------

The knob's seam forces a **single-producer shape** (its swap task is
the only thing that may flush the panel, and its opening act is the
NTP window — :doc:`the knob display <knob-display>`):

- **Warm.** ``show_moon``'s first arm (``app_main``, **before**
  ``ui_init``) stages movie frame 0 on the front buffer + descriptor +
  ``s_shown`` — the ``KNOB_UI_LAST_FRAME`` warm-arm shape — so the
  retained-panel first screen flush already carries a movie frame
  (never-black). The **saved frame takes precedence** (it is the
  latest *real* phase; the movie is a generic cycle — the
  never-a-phantom-phase discipline: a movie frame is never *claimed*
  as the current phase, and the caption is only rewritten to the real
  bake's instant by the day tick's force-rebake).
- **Loop.** The producer (``moon_task``) runs the loop **in place of
  the static opening presentation** while no trustworthy instant
  exists — the knob's whole opening act is the NTP window + the
  opening classify scan, so the loop replaces the disc the disc would
  have owned (the disc gate skips itself while ``s_movie_warmed`` is
  sticky — no disc flash between the movie and the reveal). The loop
  body is a **LUT pass only** (``knob_moon_bake_movie_frame``: no
  geometry, no erfa, no PSRAM texture loads), so it never starves the
  core the way a full bake would; each frame goes out through the
  ordinary ``s_buf[2]`` back-buffer → swap task → ONE atomic
  whole-panel ``lv_refr_now`` seam (the family invariant the movie
  rides — no new exception). Pacing waits are ≥ 10 ms = one tick at
  ``CONFIG_FREERTOS_HZ = 100``.
- **Catch-up + reveal.** Once the clock holds a plausible instant
  (SNTP fix, or a plausible NVS-restored wall clock — the same
  ``ui_now_epoch()`` the opening bake uses, so the loop can never
  catch up to a different instant than the one the reveal bakes), the
  loop maps ``now`` to the nearest frame (the clocks'
  ``hpf``-aware rounding), plays forward there (far target: one
  settle frame + the reveal), then the **opening bake** lands over
  the movie frame as the day tick's ordinary force-rebake swap.
- **No-clock edge.** No plausible instant by loop end — the loop
  exits after **one full cycle** (355 frames ≈ 44 s at the default
  pacing) and the opening bake reveals over the last movie frame
  (there is no ``now`` to aim the catch-up at; the day tick's
  late-fix re-bake takes the real phase when the fix lands).

``KNOB_UI_MOVIE_FPS`` (default 8, 2–20 — the clocks'
``UI_MOON_REPLAY_FPS`` value; the knob's wire floor is 13 ms, so 8 fps
paces the flash reads, not the panel) and the 120 s
``MOVIE_CATCHUP_BUDGET_MS`` (compile-time, the knob's own constant)
bound the catch-up leg only — the loop is unbounded, the plausible-
instant check ending it, like the clocks' bake-done check ends theirs.
Movie frames are **never persisted** (``maybe_persist_frame`` skips
them): ``moon_frame.bin`` always holds the last REAL bake.

Edge cases
----------

- **NTP not synced at boot.** The clocks' bake task polls until the
  clock syncs; the movie keeps looping (unbounded) until the frame
  lands, then play-to-current. The knob exits after one full cycle and
  reveals over the last movie frame; the late fix re-bakes through the
  force-rebake path.
- **Sleep entry / screen switch mid-loop.** ``s_replay_abort`` (clocks)
  / the arm's interrupt (knob): un-shown frames dropped, final publish
  skipped, ``s_baked_day`` stays 0 → corrective re-bake on return
  (knob: the never-black saved frame, or the last movie frame).
- **calcver mismatch.** Movie unavailable → the card fallback (whose
  folder is versioned by the same calcver, so a stale movie implies a
  stale folder → the disc floor). The partition is rewritten by the
  next ``idf.py flash``.
- **Zero-frame movie** (the row shrank below one frame's budget): the
  generator emits a valid zero-frame blob; the reader reads it as *no
  movie*; the ladder falls through. The movie never blocks a flash.
- **Wi-Fi storm.** The movie's shows ride the same drain-witnessed
  path — a storm stalls shows like any replay; the bounded
  handshakes (``REPLAY_FRAME_WAIT_MS``) release a paused or starved
  tick, so the producer can never wedge it.
- **Flash wear.** The partition is read-only at runtime.

See also
--------

The presentation's pacing arithmetic and the boot paths against the
budget — :doc:`the performance comparison
<../verification/performance>` (the boot-path ladder); the flash
budget the movie owns the leftover of — :doc:`the binary-size
chronology <../verification/binary>` and :doc:`the partitions
<../reference/partitions>`; the Kconfig keys (single-sourced
fragments) — :doc:`the Kconfig keys <../reference/kconfig-ui>` (clocks:
``UI_MOON_BOOT_REPLAY``, ``UI_MOON_REPLAY_FPS``,
``UI_MOON_REPLAY_BUDGET_MS``; knob: ``KNOB_UI_MOVIE_ENABLE``,
``KNOB_UI_MOVIE_FPS``); the animation pipelines and the retired sweep
— :doc:`the animation pipelines <animations>`; the compute progress
the reveal rides — :doc:`the compute progress <compute-progress>`; the
knob's seam — :doc:`the knob display <knob-display>`; the asset
pipeline that bakes the frames — :doc:`the Python API
<../reference/python-api>`.
