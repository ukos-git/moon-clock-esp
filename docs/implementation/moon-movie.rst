The boot movie
==============

.. brief:
   Reader: a firmware developer working on the boot presentation, the partitions or the asset pipeline.
   They already know: C, ESP-IDF partitions and CMake, the moon bake and the atomic-frame rule.
   After this page they can: explain how the movie is baked, flashed, read and played, and change its size or pacing safely.
   Page type: explanation.

At boot, every board plays a precomputed synodic cycle of moon phases while the live current phase bakes in parallel.
The portable C core bakes the movie once at build time, and the movie lives in flash, in the ``moon_movie`` data partition.
It plays looping on the panel.
When the live bake lands, the movie plays forward to the frame that matches the current phase, and the computed frame replaces it atomically.

A movie baked at build time is present on every boot: it needs no SD card, no probe and no warm cache folder.
Without it, a first boot (empty card, wiped folder or no card at all) would have nothing to show but the quick phase disc until the live bake landed.
The TF (microSD) card is the hourly refresh cache only (:doc:`the animation pipelines <animations>`).
The boot's movie path never touches the SD bus; the ~1 s mount probe happens only on the card-fallback leg, described below.

.. figure:: ../_static/figs/perf_movie.png
   :alt: Left, each board's movie size against its moon_movie partition row. Right, the movie frames of each board spread over one synodic cycle.
   :figclass: align-center

   The movie's flash cost and frame spacing.
   **Left:** each board's baked blob against its ``moon_movie`` row (the white bar is the row, the ceiling).
   **Right:** one strip per board over the 709 h from the header's ``ref_center``; every tick is one baked frame.
   The generator is ``docs/tools/gen_perf_movie.py``.

The generator fills each row with the finest phase step that fits (the "fill the space, never hinder the code build" policy).
The figure shows that the density adapts to the row (S3 and knob at 355 × 2 h, P4 at 178 × 4 h), not the playback rate.
The loop's last step is one hour of phase: frame n−1 at 708 h wraps to frame 0 ≡ 709 h, so the loop is seamless by construction.
The time paths against the 240 s budget are in :doc:`the boot figure <../verification/performance>`.
The figure script parses the manifests, the committed partition rows and the on-disk blob sizes, and asserts that they agree, so the figure and the sources cannot drift apart.

What is in the movie
--------------------

Each board's movie is one synodic cycle.
It holds one frame per ``hours_per_frame`` hour centre, spanning 709 h (29.53 d, the same span the hour cache caps at, ``TF_MAX_HOURS``).
Each frame is a q90 grayscale JPEG of a neutral bake, independent of sky events, like the stored TF-cache grays.
Classifying ~700 epochs would cost more than the animation is worth, so the final frame alone carries the event tint.

Each frame is a raw bake of the portable C core: the ``moon_sim`` path with the board's own ``--radius`` geometry, ``dark_zero`` and the neutral LUT (look-up table).
The frames are pixel-identical to a device bake, by the same provenance rule as the on-disk textures (:doc:`the C core <core-c>`).
Frame *i*'s hour centre is ``ref_center + i * hours_per_frame * 3600`` (epoch seconds, UTC).
``ref_center`` is a fixed constant in the header, 1767226800 (2026-01-01T01:00Z).
A movie's phase-to-index mapping is therefore identical across firmware versions with the same calcver.

Because the span is one synodic month, frame 0 ≈ frame n−1.
The loop wraps with at most a half-step of phase, a sub-frame jump that is invisible at the panel's drain rate.
The density of the cycle (how many frames fill it) adapts to the flash budget; the playback rate does not.
Playback is capped by the panel (~6 fps on the S3) and does not depend on how many frames the movie holds.

The container
-------------

``assets/images/moon/moon/movie.py`` (``.venv/bin/python -m moon.movie --target s3 p4 knob``) bakes one blob per board under ``assets/images/moon/out/<target>/``.
Like the other asset outputs, these are gitignored (:doc:`the Python API <../reference/python-api>`).
Each board gets ``moon_movie.bin`` and ``moon_movie_manifest.json``.

The bin starts with a 62-byte little-endian header.
It holds the magic ``MOOV``, the **calcver**, the target, ``s`` (the board's ``MOON_PX``), the disk radius, ``q90``, ``hours_per_frame``, ``frame_count``, ``ref_center``, and the table and frame offsets.
Next comes a ``frame_count`` × u32 offset table (offsets relative to the first frame), then the q90 gray JPEGs packed back to back.
The manifest carries the same facts plus the measured sizes.
It is the single source that the docs figures and the CMake guards re-derive from.

The generator adapts to the size of the row.
It reads the size of the committed ``moon_movie`` partition row; the partition CSVs are committed, static files, so the row is the source of truth.
It tries the finest phase step first (1 h/frame = 709 frames) and steps coarser (2 h, 4 h, …) until the measured JPEG total fits the row.
It needs only the cached ``moon-kit/`` and no network.
If zero frames fit, it still emits a valid zero-frame blob.
A zero-frame movie reads as *no movie* and the presentation falls back (see the ladder below), so a movie never blocks a flash.

Flash residency (all three tables)
----------------------------------

Every partition table has a ``moon_movie`` row, sized to the chip's leftover after the other rows (the single-sourced tables are in :doc:`the partitions <../reference/partitions>`):

.. list-table::
   :header-rows: 1
   :widths: 12 18 70

   * - Board
     - Row
     - Notes
   * - S3
     - ``11648 K``, last row
     - after ``moon_assets``; 355 frames @ 2 h = 10,264,284 B
   * - P4
     - ``18304 K``, last row
     - the row starts past the 16 MB mark, which is why the P4 image header declares 32 MB (:doc:`the binary-size chronology <../verification/binary>`); 178 frames @ 4 h = 12,842,691 B
   * - Knob
     - ``9088 K``, last but one
     - the 64 K ``coredump`` witness ring follows it (consumers find every partition by label, so moving the ring's offset cost nothing); the knob has no SD socket, so flash is its only movie source; 355 frames @ 2 h = 6,075,946 B

The sizing policy is "fill the space, never hinder the code build".
The app is bounded by its own ``factory`` row (S3 2048 K, P4 9216 K, knob 4096 K), so the movie can never crowd the code, and a flash always succeeds.
If the code grows, edit ``partitions.csv``: widen ``factory``, shrink ``moon_movie`` and re-run the generator.
The generator then lands on a coarser frame step automatically.
That is how the phase-frame density decreases when space runs short: it is a property of the generator, not a resize at build time.

The build chain
---------------

One ``idf.py flash`` writes the app and the movie, plus the storage SPIFFS and the ``moon_assets`` blob.
The root ``CMakeLists.txt`` and the knob's own both add ``esptool_py_flash_to_partition(flash "moon_movie" ...)``, mirroring the ``moon_assets`` rule.
Three configure-time guards run before the app compiles (the knob's ``CMakeLists.txt`` replicates the trio):

1. **The whole partition table fits the flash.** The row sizes sum to at most ``CONFIG_ESPTOOLPY_FLASHSIZE`` (16 MB S3, 32 MB P4, 16 MB knob). This is the "never hinder the code build" gate.
2. **The blob fits its row.** ``moon_movie.bin`` must be ≤ the ``moon_movie`` row. This backs up the generator; a resize mistake fails fast and names the KiB.
3. **The manifest matches the firmware.** The manifest's target and its **calcver** must equal the version the firmware is stamped with. The root ``CMakeLists`` derives ``MOON_CALC_VER`` from the ``version`` line of the commitizen ``.cz.toml`` (with a git ``describe`` fallback) and passes ``-DMOON_CALC_VER`` on every compile. The knob replicates the derive against ``../.cz.toml``. A blob baked for another board or an older core is a build-time ``FATAL_ERROR``, not a surprise at boot.

A missing blob is only a warning: the movie degrades to the card or disc path, and the code build still flashes.
A malformed manifest or a blob that does not fit is an error.

The reader
----------

:cfile:`components/moon/esp/moon_esp_movie.c` is shared by all three boards; the knob links it through ``EXTRA_COMPONENT_DIRS`` like the other shared components.
:cfunc:`moon_movie_init` finds the partition with ``esp_partition_find_first(DATA, UNDEFINED, "moon_movie")``, reads the 62-byte header, and checks the magic and the calcver.
A mismatch reads as *no movie*.
A stale movie also implies a stale card folder, because the TF cache's folder is versioned by the same calcver; the fallback then lands on the same disc or black floor that the stale folder gives.

``moon_movie_fetch(idx, gray_out, s_px)`` reads one frame's JPEG.
It decodes it through the same ``esp_new_jpeg`` codec path that the TF hour cache uses (RGB888 out, take R; a gray JPEG decodes with R == G == B) into the caller's ``s_px``-square gray bounce.
The transient decode scratch (``s_px``²·3, in PSRAM, allocated lazily) has the same shape that ``tfcache_fetch`` allocates.
The caller contract is single-threaded with no locking, the same rule as tfcache; the boot producer is the only caller.
The runtime cost is one more PSRAM bounce per board.
Internal RAM is untouched, and the partition is read-only at runtime, so there is no flash wear.

The clocks' presentation (``UI_MOON_BOOT_REPLAY``)
---------------------------------------------------

The movie is the first source under the ``UI_MOON_BOOT_REPLAY`` gate (default y); there is no separate gate.
Two core-1 tasks run the presentation.
Both have 12 KB internal-RAM stacks and priority ``configMAX_PRIORITIES − 3``, and ``moon_ui_start`` spawns them in this order (:doc:`the architecture <architecture>`).

The parallel bake (``moon_bake``)
.................................

``moon_bake`` is spawned first, so a failed spawn degrades the presentation straight to the stale-frame re-bake.
It owns the live leg.
If the clock holds no trustworthy instant yet (RTC unset, NTP not synced), it polls every 500 ms while the movie keeps looping.
Once the clock syncs, it classifies the day's sky event and bakes the current phase into the shared ``s_hourly_gray`` bounce with the compute-progress watermark (``COMPUTE_REPLAY_FINAL``).
The seconds arc sweeps its rows-completed bar, as on the hourly leg (:doc:`the compute progress <compute-progress>`).
At the end it sets ``s_boot_bake_done``, or ``s_boot_bake_failed`` on an abort, missing time or missing albedo.

The strip loop yields per strip (``vTaskDelay(1)`` inside :cfunc:`moon_bake_progress`).
The producer runs on the same core at the same priority, so it keeps feeding movie frames while the bake runs.
The bake task never touches ``s_frame``, ``s_rgb`` or the panel; the producer owns the publish.
The two tasks never share a buffer: the bake owns ``s_hourly_gray``, and the producer plays from its own fetch bounce.

The producer (``moon_replay``) and its source ladder
....................................................

The producer plays the first source on this ladder that is available:

1. **The flash movie**, when :cfunc:`moon_movie_init` finds it valid (magic, calcver and ``s`` match, and at least one frame).
   The frames play looping, one frame per tick, with the neutral LUT (``moon_gray565_set_event`` off, because the frames do not depend on sky events).
   Each publish happens in one ``s_rgb_mtx`` block.
   The tick lands each show as one atomic whole-panel ``lv_refr_now`` under ``s_rgb_mtx``, drain-witnessed.
   The movie adds no exception to the atomic-frame invariant: its reads happen off-panel on core 1, and its shows ride the sanctioned atomic-show path.
   The loop has no time bound; the loop is the animation, and the bake completing ends it.
   A repeating fetch failure (100 consecutive misses, from a torn partition or a flash error) stops the loop and degrades to the final leg; a single miss that heals is skipped.
2. **The card fallback**, when there is no partition, the calcver does not match or the movie has zero frames.
   This is the card replay of :commit:`dabbf475`, unchanged.
   The lazy mount happens at the start of this leg, so a fallback without a card pays the ~1 s probe here (the movie path never touches the bus at boot).
   The stored hours play ascending with the neutral tint, and ``UI_MOON_REPLAY_BUDGET_MS`` bounds the leg.
3. **The floor**: the quick phase disc that ``moon_ui_start`` painted before the spawn (or black, without a trustworthy time) stays up under both.

Catch-up and the final frame
............................

When ``s_boot_bake_done`` flips, the producer maps the current hour to the nearest loop index by hour arithmetic on the header's ``ref_center``.
There is no new-moon scan.
Frame *i*'s centre is ``ref + i·hpf`` hours, so the nearest frame is within ``hpf/2`` hours of the current phase, and the swap over it is seamless.
A target far ahead (more than ``n/4`` of the cycle, ``REPLAY_CATCHUP_QUARTER``) gets one settle frame at the target, then the final frame; this is a hard cut, acceptable on a loading screen.
A near target plays forward one frame per tick to the current phase, bounded by the budget.

Both sources then share the final leg:

* A bounded wait, if the bake is still running. The budget cuts the animation, never the moon: a wait that overruns degrades to the stale-frame re-bake backstop.
* A bounded handshake that the last published frame has landed before the current frame replaces it. The presentation plays to the current phase, and then shows the computed image.
* The current frame, published under ``s_rgb_mtx`` with the bake's event tint. The producer is the presentation's only LUT writer.
* The bookkeeping (``s_orbit_epoch``, ``s_baked_hour``, ``s_baked_event``, ``s_baked_day``), committed here and nowhere else.
* The last atomic show, landed by the tick and drain-witnessed as ``boot replay final``.

The card's new-moon prune (the ~8 s ``new_moon_epoch`` scan) also rides the final leg.
Movie frames never move ``s_orbit_epoch``, because they are historic phases; only the final bake may.

Pacing and the budget
.....................

The producer paces at a floor of ``1000 / UI_MOON_REPLAY_FPS`` (default 8, so at least 125 ms per frame).
The actual rate is what the 50 ms tick and the whole-panel flush allow on top: ~6 fps measured on the S3 (:doc:`the performance comparison <../verification/performance>`).
``UI_MOON_REPLAY_BUDGET_MS`` (240 s default) bounds only the bounded legs: the card-fallback leg and the catch-up leg.
The movie loop itself has no time bound; the bake stops it, and the clock sync is what lets the bake finish.

Arbitration and abort
.....................

While ``moon_ui_replaying()`` is true, the hourly probe and spawn, the day-tick and the orbit stay out, and every idle re-arm site defers.
The three core-1 producers (replay, parallel bake, hourly) therefore never run at once, which keeps tfcache's single-caller contract intact.

A sleep entry or a screen switch away from the moon raises ``s_replay_abort``.
The frames not yet shown are dropped, the final publish is skipped and ``s_baked_day`` stays 0, so the minute day-tick queues the corrective re-bake on return.
``s_frame_stale`` marks the degraded run.
The panel rests on the last complete frame (a movie frame or the disc), so there is nothing to heal.

Demo mode boots without the presentation, because its scripted tour owns the display.
With ``UI_MOON_BOOT_REPLAY=n`` the clock boots straight to the static current phase and touches no SD bus.

The knob (``KNOB_UI_MOVIE_ENABLE``)
-----------------------------------

The knob's seam forces a single-producer shape.
Its swap task is the only thing that may flush the panel, and its opening act is the NTP window (:doc:`the knob display <knob-display>`).

Warm arm
   ``show_moon``'s first arm (in ``app_main``, before ``ui_init``) stages movie frame 0 on the front buffer, the descriptor and ``s_shown``, the same shape as the ``KNOB_UI_LAST_FRAME`` warm arm.
   The first screen flush of the retained panel therefore already carries a movie frame, so the panel is never black.
   The saved frame takes precedence, because it is the latest real phase and the movie is a generic cycle.
   This is the knob's rule against phantom phases: a movie frame is never claimed as the current phase, and only the day tick's force-rebake rewrites the caption to the real bake's instant.

Loop
   The producer (``moon_task``) runs the loop in place of the static opening presentation while no trustworthy instant exists.
   The knob's whole opening act is the NTP window plus the opening classify scan, so the loop takes the slot the disc would otherwise hold.
   The disc gate skips itself while ``s_movie_warmed`` is set (it stays set), so no disc flashes between the movie and the final frame.
   The loop body is a LUT pass only (``knob_moon_bake_movie_frame``: no geometry, no erfa, no PSRAM texture loads), so it never starves the core the way a full bake would.
   Each frame goes out through the ordinary ``s_buf[2]`` back buffer, the swap task and one atomic whole-panel ``lv_refr_now``, the same seam the family invariant requires; the movie adds no exception.
   Pacing waits are ≥ 10 ms, one tick at ``CONFIG_FREERTOS_HZ = 100``.

Catch-up and the final frame
   Once the clock holds a plausible instant (an SNTP fix, or a plausible wall clock restored from NVS), the loop maps ``now`` to the nearest frame with the clocks' ``hpf``-aware rounding.
   It uses the same ``ui_now_epoch()`` as the opening bake, so the loop cannot catch up to a different instant than the one the final frame bakes.
   It plays forward to that frame (a far target gets one settle frame), and then the opening bake lands over the movie frame as the day tick's ordinary force-rebake swap.

No clock
   With no plausible instant by the end of the loop, the loop exits after one full cycle (355 frames ≈ 44 s at the default pacing).
   The opening bake then lands over the last movie frame, because there is no ``now`` to aim the catch-up at.
   The day tick's late-fix re-bake takes the real phase when the fix arrives.

``KNOB_UI_MOVIE_FPS`` defaults to 8 (range 2–20), the value of the clocks' ``UI_MOON_REPLAY_FPS``.
The knob's wire floor is 13 ms, so 8 fps paces the flash reads, not the panel.
That rate and the 120 s ``MOVIE_CATCHUP_BUDGET_MS`` (a compile-time constant of the knob) bound only the catch-up leg.
The loop has no time bound; the plausible-instant check ends it, as the bake-done check ends the clocks' loop.
Movie frames are never persisted (``maybe_persist_frame`` skips them), so ``moon_frame.bin`` always holds the last real bake.

Edge cases
----------

- **NTP not synced at boot.** The clocks' bake task polls until the clock syncs; the movie keeps looping until the frame lands, then plays to the current phase. The knob exits after one full cycle and shows the final frame over the last movie frame; the late fix re-bakes through the force-rebake path.
- **Sleep entry or screen switch mid-loop.** ``s_replay_abort`` (clocks) or the arm's interrupt (knob) fires. Frames not yet shown are dropped, the final publish is skipped and ``s_baked_day`` stays 0, which triggers the corrective re-bake on return (on the knob: the never-black saved frame, or the last movie frame).
- **calcver mismatch.** The movie is unavailable, so the card fallback runs. Its folder is versioned by the same calcver, so a stale movie implies a stale folder and the disc floor. The next ``idf.py flash`` rewrites the partition.
- **Zero-frame movie** (the row shrank below one frame's budget). The generator emits a valid zero-frame blob, the reader reads it as *no movie*, and the ladder falls through. The movie never blocks a flash.
- **Wi-Fi storm.** The movie's shows ride the same drain-witnessed path, so a storm stalls them like any replay. The bounded handshakes (``REPLAY_FRAME_WAIT_MS``) release a paused or starved tick, so the producer can never wedge it.
- **Flash wear.** None; the partition is read-only at runtime.

Where to go next
----------------

* :doc:`The performance comparison <../verification/performance>`: the pacing arithmetic and the boot paths against the budget (the boot-path ladder).
* :doc:`The animation pipelines <animations>`: the boot replay, the hourly refresh and the sweep the movie replaced; :doc:`the compute progress <compute-progress>` covers the progress bar during the final bake.
* :doc:`The knob display <knob-display>`: the knob's seam.

The flash budget the movie fills is in :doc:`the binary-size chronology <../verification/binary>` and :doc:`the partitions <../reference/partitions>`.
The Kconfig keys are in :doc:`the Kconfig keys <../reference/kconfig-ui>` (clocks: ``UI_MOON_BOOT_REPLAY``, ``UI_MOON_REPLAY_FPS``, ``UI_MOON_REPLAY_BUDGET_MS``; knob: ``KNOB_UI_MOVIE_ENABLE``, ``KNOB_UI_MOVIE_FPS``).
The asset pipeline that bakes the frames is in :doc:`the Python API <../reference/python-api>`.
