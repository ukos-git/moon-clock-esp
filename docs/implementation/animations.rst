The animation pipelines
=======================

.. brief:
   Reader: a developer working on the home's live motion: the orbit, the hourly
   refresh, the boot presentation, the fade.
   They already know: C, FreeRTOS tasks, the atomic-display rule.
   After this page they can: trace each live pipeline from trigger to the atomic show.
   Page type: explanation.

A moon clock is mostly *waiting* — waiting for the next minute, the
next hour, the next wake. The home has three live pipelines, all built
on the one atomic-display rule (:doc:`the architecture
<architecture>`) and the one panel-write discipline (:doc:`the display
pipeline <display>`):

1. **The orbit** — a *response*, not an animation: the tilt viewport
   re-bake, one atomic frame per sustained pose change.
2. **The hourly refresh** — the moon redraws once per hour with the
   old frame up the whole cycle: the producer produces the
   current-phase frame (a cache hit or a cold bake, the seconds arc
   sweeping the bake's rows watermark as a progress bar —
   :doc:`the compute progress <compute-progress>`), the finished
   frame landing as one atomic whole-panel flush in the bright panel.
   No 0x51 traffic at all.
3. **The fade** — the screen-switch dim (and the light-sleep dim and
   wake ramp), which writes no pixels. It is the one
   hardware-brightness-ramp exception to the atomic-display rule
   (:doc:`the architecture <architecture>`).

Light sleep is a **static dim frame**: panel to 25 %, clock to 20 %
alpha, and the deep-sleep countdown armed at the entry itself
(:doc:`the clock and sleep machine <clock-sleep>`).

At boot the home **presents the boot movie** (:doc:`the boot movie
<moon-movie>`): a precomputed synodic cycle baked at build time and
flashed into the ``moon_movie`` data partition plays **looping** while
a second core-1 task bakes the live current phase in parallel (the
seconds arc sweeping the bake's rows watermark as a progress bar, the
compute section's dummy — :doc:`the compute progress
<compute-progress>` — is the floor under both); when the bake lands
the movie plays forward to the frame matching the current phase and
the live-computed frame lands as the final frame, one atomic whole-
panel flush over it (with no valid movie the card's stored hours play
ascending instead, and with neither source the quick phase disc holds
until the final bake lands — *The boot presentation*, below). It is
storage playback — the flash-resident movie first, the hour cache as
fallback; with ``UI_MOON_BOOT_REPLAY=n`` the home boots straight to
the static current phase (no SD bus at boot either way).

The home's moon is plain LVGL, shown static. The motion pipelines that
existed before — the free spin, the computed boot sweep, the dusk —
are summarized in :doc:`the design history <../project/history>`,
together with their per-frame measurements. The TF card cache module
serves the hourly refresh as the **TF hour cache** (below).

**Burn-in is accepted.** The S3's AMOLED shows the moon in one place
for weeks on end, so a faint burn-in of the moon into the panel is
*likely* over the device's life. That is a settled product decision
(:doc:`the design history <../project/history>`), not an oversight: a
burned-in moon counts as a **feature rather than a bug** — the ghost
is the clock's own subject, and it will likely not disturb reading the
display. Re-introducing any anti-burn-in machinery (a rotation, a
pixel nudge, anything) needs a fresh product decision first.

The remaining pipelines share one pipeline shape: a *bake task* (core 1) produces
frames off screen into the shared PSRAM buffers; the *tick* (LVGL
task, 50 ms) displays each published frame as **one atomic whole
frame**; backpressure (the bake waits for the display) keeps the two
from tearing a frame in half.

The hourly refresh (``UI_MOON_HOURLY_ENABLE``)
----------------------------------------------

The one live motion left on the home: once per hour the moon redraws
with the **old frame up the whole cycle** — no fade, no black window
(the :commit:`ee4e3efd` compute-progress change retired the fade the
original brief asked for, *"Update the moon hourly with a fade to
black, redraw and fade-in"*: with a computed image present the product
stays on it, and the compute shows in the seconds arc instead). The
clock, the alarm row and the seconds keep running through the whole
cycle; the new frame simply lands over the old one as ONE atomic
whole-panel flush (:doc:`the compute progress <compute-progress>`).

It is a producer/consumer split, exactly like the deferred re-bake, so
the LVGL task is never blocked on a bake (the three-role shape, all in
``components/ui/moon_ui.c`` under the ``hourly`` banner):

* **The probe** (``moon_hourly_probe``) runs on the minute path
  (``moon_ui_day_tick``). It gates and *arms* — records the hour-centre
  epoch and ``s_hourly_start`` — doing no panel work. It fires when the
  on-screen hour (``s_baked_hour``) is no longer the current hour, and it
  skips when the device is idle (light sleep), a moon-less screen is
  active, the orbit / re-bake / another hourly owns the buffers, or a
  screen-switch fade / 0x51 ramp owns the panel.
* **The tick legs** (``moon_hourly_service``, LVGL task) own NO 0x51
  traffic: the start leg simply spawns the producer (the gates park it
  while a screen-switch fade or light-sleep ramp owns the panel), and
  the done leg shows the finished frame as **ONE atomic whole-panel
  flush in the bright panel** — the old frame is up the whole cycle, so
  both a **TF fetch hit** and a **cache miss** land the same way. A
  producer that misses its 5 s budget (``HOURLY_BUDGET_MS``) leaves the
  OLD frame (nothing to restore — it is still what the panel shows);
  a late producer that lands after the deadline still shows on its own
  done leg.
* **The producer** (``moon_hourly_task``, core 1, 12 KB, latest-wins)
  fills a PSRAM bounce: it asks the **TF hour cache** first (a hit is a
  JPEG decode instead of a ~1.3 s (S3) / ~2.4 s (P4) bake — a hit runs
  no strip loop, so the arc shows real seconds), else it bakes
  strip-chunked with the rows-completed watermark published under
  ``s_rgb_mtx`` (``moon_bake_progress``, the same output as
  ``moon_bake`` — :doc:`the compute progress <compute-progress>`)
  with write-through store + eviction; the watermark drives the
  seconds-arc progress bar. A sleep entry mid-bake sets
  ``s_compute_stop`` and the producer deletes itself *before* the
  publish (the partial bounce never shows, never stores). It classifies
  the hour-centre instant once (the gray→RGB565 tint LUT + the
  blood-moon occlusion), then publishes ``s_frame`` + ``s_rgb`` in ONE
  ``s_rgb_mtx`` block. It never touches the panel.

The whole cycle, role by role — who touches the panel, who touches the
card, and where the two legs' timings land:

.. raw:: html

   <pre class="mermaid">
   sequenceDiagram
       participant MIN as Minute path (probe)
       participant TICK as LVGL tick (legs)
       participant P as Producer core 1
       participant TF as TF hour cache
       participant PN as Panel

       MIN->>MIN: gate (idle, moon-less screen, buffers busy, a fade owns the panel) then arm s_hourly_start
       MIN->>TICK: armed
       TICK->>P: spawn (latest-wins, 5 s budget) — the OLD frame stays up (no 0x51 traffic)
       P->>TF: fetch the hour-centre key (hour*3600 + 1800)
       alt card hit
           TF-->>P: stored gray JPEG (no strip loop, arc shows real seconds)
           P->>P: tint LUT, publish s_frame + s_rgb in one s_rgb_mtx block
           TICK->>PN: ONE atomic whole-panel flush in the BRIGHT panel, drain-witnessed
       else card miss (compute progress)
           P->>P: moon_bake_progress strip-chunked, rows watermark under s_rgb_mtx (the seconds arc sweeps it)
           P->>TF: write-through store + eviction (max of synodic-back and new-moon floor)
           P->>P: publish s_frame + s_rgb in one s_rgb_mtx block
           TICK->>PN: ONE atomic whole-panel flush in the BRIGHT panel, drain-witnessed
       else sleep entry mid-bake (s_compute_stop)
           P->>P: deletes itself BEFORE the publish — the partial bounce never shows
           Note over MIN,PN: wake re-arms the cycle (s_baked_hour never committed)
       else producer misses the 5 s budget
           Note over TICK,PN: nothing to restore — the OLD frame is still up
       end
       Note over MIN,PN: s_baked_hour + s_baked_event commit ON SHOW — a cycle that never shows stays due and self-heals on the next minute tick
   </pre>

The bake key is the **hour centre** (``hour * 3600 + 1800``): stable
across boots and boards, so a card seeded by one boot serves the next
the byte-identical gray, and the on-screen frame is at most 30 min stale
at the show (:ref:`the freshness ladder <freshness-ladder-perf>` plots
this bound beside every other one on the panel, parsing the 1800 s
offset at figure time). ``s_baked_hour`` / ``s_baked_event`` commit *on show*, so a
cycle that never shows leaves the next hour still due (it self-heals on
the next minute tick).

Sleep hand-off. If a sleep entry lands mid-cycle, ``moon_ui_pause(true)``
sets ``s_compute_stop`` and the producer deletes itself *before* the
publish (the partial bounce never shows, never stores — the watermark's
bounce-only rule), and drops the pending legs before pausing the tick:
the panel keeps the consistent OLD frame into the sleep screens, and the
wake's day-tick re-arms the cycle (``s_baked_hour`` was never committed
on show).

**Bench hook.** ``CONFIG_TEST_HOURLY_FORCE_S=N`` fires the full cycle
every N seconds while the home is active and not idle — acceptance for
the fade/produce/show legs (cold card = miss + bake + store; warm card =
fetch), without waiting on a real hour boundary.

.. _calcver-keying:

The TF hour cache (``UI_MOON_TFCACHE`` + ``UI_MOON_HOURLY_TFCACHE``)
.....................................................................

The anatomy as a sealed overview plus two detail sketches — what a
warm hour is, where the folder's key comes from, who may touch the
card (never both producers at once), and why a missing card is a
normal path rather than an error:

.. graphviz:: ../_graphs/tf-hour-cache.dot
   :alt: The TF hour cache sealed — folder keyed by calcver/target/bakepx, the card with one gray JPEG per hour and a bounded ≈709-file window, the two core-1 producers single-caller by construction, the lazy first-producer mount, tfcache_ready false as the normal card-less posture
   :caption: The sealed view — the folder key, the card, the two core-1 producers (never concurrent), the lazy mount whose first producer pays the ~1 s card-less probe once, and ``tfcache_ready()`` false as the NORMAL, silent posture (missing card, failed mount, bad decode, card pulled mid-run — the refresh just bakes every hour, exactly like the knob's card-less path).
   :align: center

.. graphviz:: ../_graphs/tf-hour-cache-card.dot
   :alt: The card opened — folder named calcver underscore target underscore bakepx, one hour-centre gray JPEG per hour stable across boots and boards, meta.txt whose resolution mismatch wipes the folder while its non-authoritative new-moon floor line never does, the bounded ≈709-file window agreed by list, store and evict
   :caption: On the card — the folder ``/moon_cache/<calcver>_<target>_<bakepx>/``, the hour key at ``hour·3600 + 1800`` making the gray JPEG stable across boots AND boards (one card serves them all), ``meta.txt`` whose resolution mismatch wipes and recreates the folder while its NON-authoritative floor line never does, and the bounded window — the floor is the newer of one synodic back and the new-moon floor the producers supply, and ``list_hours`` / store / evict all agree on it (``moon_tfcache.c``).
   :align: center

.. graphviz:: ../_graphs/tf-hour-cache-flow.dot
   :alt: The producers opened — the boot replay listing hours ascending and presenting first, the hourly probe holding off while it does, a fetch HIT tinting and publishing in 185–235 ms, a MISS baking then write-through storing, the lazy once-only mount and the not-ready normal posture
   :caption: The producers — the boot replay lists hours ASCENDING, presents first, and the hourly probe/spawn holds off while it does (two core-1 producers, never concurrent); a fetch HIT tints the LUT and publishes in 185–235 ms (old-frame-then-atomic — the hourly owns no black window since :commit:`ee4e3efd`), a MISS bakes strip-chunked (the rows watermark drives the seconds arc) then WRITE-THROUGH stores so the next fetch hits, and the replay ends in its now-bake FINAL leg that commits on show; init runs once, driven by the FIRST producer — a card-less boot pays one ~1 s probe and the scratch stays resident (the decode out-buffer remains lazy).
   :align: center

The cache persists one small gray JPEG per HOUR under
``/moon_cache/<calcver>_<target>_<px>/``; the folder name carries the
calculation version — the version the repo sets **manually via
commitizen** (``cz bump`` writes ``version = "x.y.z"`` into ``.cz.toml``,
and the root ``CMakeLists`` reads that line as ``MOON_CALC_VER``); the
``git describe`` short hash is the pre-bump fallback only — the first
bump (0.1.0 → 0.2.0, :commit:`97d1ebd7`) made the version line the live key —
so a deliberate version bump starts an empty folder while plain commits
leave the card alone, and a self-describing ``meta.txt`` wipes/recreates
the folder on any resolution mismatch (``moon_tfcache.c``). Two rules bound the folder
(the cache policy, :commit:`dabbf475`): nothing more than one synodic
month of hourly keys survives (~709 files), **and** everything older
than the last NEW MOON is cleared at that crossing. The floor needs the
ephemeris, which this module does not link, so the core-1 producers
compute it — ``new_moon_epoch`` (the restored two-pass illumination scan,
*The boot sweep* below) hands the hour key through
``tfcache_set_new_moon_floor()``, ``tfcache_evict()`` and
``tfcache_list_hours()`` apply ``max(synodic-back, new-moon)``, and the
floor rides ``meta.txt`` as a **non-authoritative** line (it never
invalidates the folder), so even a boot without a trustworthy clock
prunes and replays on the last known new moon. The hourly producer
re-runs the scan + evict every hour, so a board that never reboots still
prunes at the crossing. What those two rules cost — the per-frame codec
scratch (resident pair vs the lazy pair) and the bounded worst case of a
full 709-file card month, per board — is quantified in
:doc:`the performance comparison <../verification/performance>`
(*The hour-cache footprint*).

The producer mounts the card **lazily on the first producer to run** —
the boot replay task when ``UI_MOON_BOOT_REPLAY`` is on (a card-less
boot then pays ~1 s of mount probe before the now path starts — the
panel holds the quick phase disc meanwhile),
otherwise the first hourly refresh, in which case boot never touches the
SD bus — and every card failure (missing card, failed mount, failed
decode, a mid-run pull, a store error) flips ``tfcache_ready()`` false
for the boot and the refresh silently bakes on-device (the card-less
path). The fetched frame runs through the SAME ``moon_gray565()`` tint
LUT as a baked frame, so cache and compute paths are pixel-identical by
construction.

The boot presentation (``UI_MOON_BOOT_REPLAY`` + the TF keys)
...............................................................

The brief (:commit:`dabbf475`): *"on bootup, during re-calculation of
the current moon, all displays should show all previously calculated
frames as an animation and stop at the last calculated frame that was
stored on the TF card… If no cache is available, the display stays
black until the current moon phase is calculated." On :commit:`7693bffa` the
presentation was **re-sourced** (:doc:`the boot movie <moon-movie>`):
the "previously calculated frames" now come first from a
**flash-resident movie** — the precomputed synodic cycle baked at
build time into the ``moon_movie`` data partition — and the
"re-calculation" half runs the live bake **in parallel** with a
looping movie instead of after a one-shot playback. The TF card is
demoted: it is the **hourly refresh cache** (below) now, and its
stored hours play only as the movie's fallback source. The whole page
of mechanics — the container, the generator, the flash residency, the
reader, the catch-up math, the knob's single-producer shape — is
:doc:`the boot movie <moon-movie>`; this section keeps the presentation
shape and the arbitration it rides on.

It is still the hourly split reused (``components/ui/moon_ui.c``, the
``replay`` / presentation section), with **two** core-1 tasks (12 KB
internal-RAM stacks, the hourly/rebake priority) spawned by
``moon_ui_start``:

**The parallel bake (``moon_bake``)** — spawned FIRST (a failed spawn
degrades the presentation straight to the stale-frame re-bake). If the
clock holds no trustworthy instant yet it polls at 500 ms — the movie
keeps looping meanwhile — and once the clock syncs it classifies the
day's sky event and bakes the current phase into the shared
``s_hourly_gray`` bounce with the compute-progress watermark (the
seconds arc sweeps its rows-completed bar — the "during
re-calculation" half), setting ``s_boot_bake_done`` (or
``s_boot_bake_failed`` on abort / no time / no albedo). The strip loop
yields per strip (``vTaskDelay(1)`` in ``moon_bake_progress``), so the
producer — same priority, same core — keeps feeding frames while the
bake runs. It never touches ``s_frame`` / ``s_rgb`` / the panel: the
producer owns the publish, and the two tasks never share a buffer.

**The producer (``moon_replay``)** — plays the **source ladder**:

1. **The flash movie** (valid header: magic + calcver + board size +
   at least one frame): the frames play **LOOPING** — one frame per
   tick, wrapping (frame 0 ≈ frame n−1, so the loop is seamless by
   construction), neutral tint LUT (the frames are event-independent
   bakes; classifying ~700 epochs would cost more than the animation
   is worth), each publish in ONE ``s_rgb_mtx`` block from the
   producer's OWN fetch bounce — the exact hourly publish shape. The
   loop is unbounded: the loop IS the animation, and the bake
   completing is what ends it.
2. **The card fallback** (no partition / calcver mismatch /
   zero-frame movie): the :commit:`dabbf475` card replay verbatim — enumerate
   the cached hours ascending (``tfcache_list_hours``; the lazy mount
   happens on THIS leg's first moment, so a card-less fallback pays
   the ~1 s probe here — the movie path never touches the SD bus at
   boot), fetch each gray JPEG, neutral tint, publish in ONE
   ``s_rgb_mtx`` block per frame.
3. **The floor**: the **quick phase disc** ``moon_ui_start`` painted
   before the spawn (or black without a trustworthy time — no epoch,
   no disc) stays up under both.

It never touches the panel. The **tick** lands every published frame
as ONE atomic whole-panel ``lv_refr_now`` under ``s_rgb_mtx``, each
show **drain-witnessed** (up to ~709 shows per boot — witnessing only
the last one would let a lost completion pin the next ~2 s per strip).
No fade: the brightness never moves. Pacing is a floor
(``UI_MOON_REPLAY_FPS``, min dwell per frame) plus a single in-flight
frame (the producer waits for the tick to consume the show flag; every
wait is bounded so a paused tick can never wedge it).
``UI_MOON_REPLAY_BUDGET_MS`` now bounds the **bounded legs only** —
the card-fallback leg and the catch-up leg; the movie loop is
unbounded. Overrun stops the **animation**, never the moon: the final
leg still lands the current phase. The pacing arithmetic — the
``1000/REPLAY_FPS`` floor, the observed ~6 fps drain, the 709-file cap
and the budget — is plotted in
:doc:`the performance comparison <../verification/performance>`.

**Catch-up and the final leg.** When ``s_boot_bake_done`` flips, the
producer maps the now hour to the **nearest** loop index by hour
arithmetic on the movie header's ``ref_center`` — no new-moon scan:
frame *i*'s centre is ``ref + i·hpf`` hours, so the nearest frame is
within ``hpf/2`` hours of the now-frame's phase. A far target (more
than ``n/4`` of the cycle ahead) takes one settle frame at the target
then the reveal (a hard cut); a near target **plays forward one frame
per tick** to the current phase, budget-bounded. Then the **final
leg** — common to both sources: a bounded wait if the bake is still
running, a bounded handshake that the last published frame has
*landed* before the now-frame replaces it ("play to the current
phase, **then** reveal the computed image"), the now-frame published
under ``s_rgb_mtx`` with the bake's event tint, the bookkeeping
committed there and only there (``s_orbit_epoch`` / ``s_baked_hour`` /
``s_baked_event`` / ``s_baked_day``), and the tick landing it as the
last atomic show, drain-witnessed as ``boot replay final``. The card's
new-moon prune (the ~8 s ``new_moon_epoch`` scan) still rides the
final leg. Movie and card frames never move ``s_orbit_epoch`` — they
are historic phases; only the final bake may. No clock at all: the
bake task polls, the movie keeps looping (unbounded), and the frame
lands once the clock syncs — the day-tick re-bake is the backstop
(without a trustworthy time the panel is black, the pre-replay path,
which has no epoch and therefore no disc). The **knob** has no SD
socket — flash is its ONLY movie source — and its seam forces a
single-producer shape: the loop runs inside the producer task in place
of the static opening presentation, and the opening bake lands over
the movie frame as the day tick's force-rebake swap
(:doc:`the boot movie <moon-movie>`, :doc:`the knob display
<knob-display>`). Demo mode boots without the presentation (its
scripted tour owns the display).

The whole presentation, including the abort path and the single-caller
arbitration the ladder rests on:

.. raw:: html

   <pre class="mermaid">
   flowchart TB
     BOOT["Boot, UI_MOON_BOOT_REPLAY on, not demo"] --> SPAWN["spawn: moon_bake FIRST (parallel live bake, polls until clock syncs) + moon_replay; idle ladder held; disc painted"]
     SPAWN --> BAKE["moon_bake — classify + now-bake into s_hourly_gray (arc sweeps its rows watermark); yields per strip"]
     SPAWN --> LADDER{"moon_movie valid? (magic + calcver + size)"}
     LADDER -- "yes" --> LOOP["the flash movie LOOPING — one frame per tick, neutral LUT, publish under s_rgb_mtx (NO SD bus)"]
     LADDER -- "no partition / calcver mismatch / zero frames" --> LIST{"card fallback — mounted and hours listed (lazy mount HERE pays the ~1 s probe)"}
     LIST -- "hours ascending" --> FETCH["per hour — fetch gray JPEG, neutral tint, publish under s_rgb_mtx (the budget bounds this leg)"]
     FETCH --> SHOWC["tick lands each frame as ONE atomic whole-panel flush, drain-witnessed"]
     SHOWC --> MORE{"more hours?"}
     MORE -- "yes" --> FETCH
     MORE -- "no, stopped on newest" --> WAIT
     LOOP -- "s_boot_bake_done" --> CATCH["catch-up — nearest loop index from ref_center (hpf-aware rounding): far = one settle frame + reveal; near = play forward one frame per tick (the budget bounds this leg)"]
     LIST -- "no card, cold folder, version wipe" --> WAIT
     BAKE --> WAIT
     WAIT["waits out the bake, budget-bounded (s_boot_bake_done)"]
     WAIT --> FINAL["final leg — handshake (last frame landed), now-frame published under s_rgb_mtx with the bake's event tint"]
     CATCH --> FINAL
     FINAL --> LAST["tick's last atomic show (drain-witnessed as 'boot replay final'), bookkeeping commits, end leg re-arms the idle ladder"]
     LOOP -. "sleep entry or screen switch aborts — un-shown frames dropped, final publish skipped" .-> ABORT["s_baked_day stays 0 — the minute day-tick queues the corrective re-bake on return"]
     BAKE -. "aborted mid-bake — s_boot_bake_failed" .-> ABORT
   </pre>

While ``moon_ui_replaying()`` is true the hourly probe/spawn, the
day-tick and the orbit gate out and every idle re-arm site in
``actions.c`` defers — the three core-1 producers (replay, parallel
bake, hourly) never run at once, which is what keeps tfcache's
single-caller contract intact (the parallel bake never touches the
card at all — the contract is about the card, not the core).

Lifecycle holds (the boot presentation owns the screen for its
length): the spawn disables the idle→sleep ladder and the tick's
**end leg** re-arms it when the presentation is over (every idle
re-arm site in ``actions.c`` — boot wake, WiFi-sync hand-off,
airplane switch, the test hooks — defers while
``moon_ui_replaying()``); the hourly probe/spawn, the day-tick and the
orbit gate on the same signal, so no fade cycle, corrective re-bake or
orbit peek races the presentation over ``s_frame`` / ``s_rgb`` / the
card (tfcache's single-caller contract holds: the producers never run
at once). A sleep entry (``moon_ui_pause(true)``) or a screen switch
away from the moon **aborts** the presentation — the un-shown frames
are dropped, the final publish is skipped (``s_baked_day`` stays 0, so
the minute day-tick queues the corrective re-bake once the device is
back on the home), and a degraded run sets ``s_frame_stale`` — the
panel rests on the last COMPLETE frame (a movie frame or the disc);
nothing to heal.

The boot sweep (removed)
------------------------

The computed boot animation (the current synodic month played at boot,
1344 baked frames) is gone: no per-frame bake task, no montage. With
``UI_MOON_BOOT_REPLAY=n``, ``moon_ui_start`` bakes the current phase
directly and boot is one static frame, like every wake. The boot
presentation (*above*) is a different animal: it plays **stored** hour
frames — pure playback, one JPEG decode per frame, no bake — and ends
on the newest stored hour plus the computed now-frame. ``new_moon_epoch``
lives on as the TF hour cache's new-moon floor (not a sweep span). The
knob has no SD socket and boots to the single static frame; a boot
without a saved frame holds the quick phase disc instead of black
(:doc:`the compute progress <compute-progress>`, :doc:`the knob
display <knob-display>`). What the sweep did, and what its TF-cache
era established, is in :doc:`the design history
<../project/history>`.

Who pays for the day scan
..........................

The classification RULES (the ±18 h moment test around the exact
opposition, the census-pinned super/micro bounds, the umbra/penumbra cone,
the 742-moon census itself) are single-sourced in
:ref:`the sky events <sky-events-capi>`; what belongs to the UI story is
the *economics* — the scan is one 6 h-grid sweep of a 60-day window
(the code's own cap: *up to ~600* ``se_dot`` evaluations, and the
knob-only ``s_scan_evals`` INFO line prints the true number once a day)
and it must never run on the LVGL task
(the :commit:`36f1a635` root cause: it froze the clock between minute ticks and
storm-printed the unwatched IDLE0 until the WDT fired). Four facts pin the
shape: the five core-1 pipeline tasks (hourly, replay, rebake, orbit, and
the worker itself — ``moon_ev_scan_allowed``) may scan IN PLACE and warm
the day on the way; every other caller passes
``moon_event_classify_here``, which on a cold day hands the scan to the
worker and returns a ZEROED event — a plain untinted bake, byte-identical
to an all-false classify, that SELF-HEALS a minute later when the probe
sees the signature change; the worker is pinned to the unwatched core with
a priority BELOW the bake tasks (a hungry bake outranks a warm-up); and the
knob, having no worker, compiles a 20 ms wall-clock yield budget INTO the
scan loops themselves — the bench lesson that a fixed every-32-evals yield
still starved IDLE1 (the argmax's 73 samples never yielded at all).

The whole caller picture — who in the shipped tree calls each of the
core's 23 entries — is the surface sketch in :doc:`the C API
<../reference/c-api>`; its amber guard node is exactly this wrapper.

.. graphviz:: ../_graphs/sky-events.dot
   :alt: The sky-events chain sealed — classify_here as the only gate every caller walks, the core-1 worker, the once-a-day scan cached per calendar day and never on LVGL, and the three consumers: tint LUT, eclipse bite, minute probe
   :caption: The sealed view — every caller walks ``moon_event_classify_here`` (:1122); the five core-1 pipeline tasks (``moon_ev_scan_allowed``, :1097) may classify IN PLACE and warm the day, every other caller on a cold day hands the scan to the worker and gets a ZEROED event — a plain untinted bake that self-heals the next minute when the probe sees the signature change. The minute day-tick runs the pipeline-busy guard BEFORE the classify on purpose (:2707+): a running pipeline task classifies its own epoch and warms the day for free, and on a cold day ``classify_here`` returns FALSE and the tick simply returns. The worker (:1103, spawn :2614) is core-1 and IDLE1-unwatched (the 15–20 s scan can never trip a watchdog), carries a 12 KiB era-chain SPIRAM stack at priority MAX−4 so a hungry bake outranks a warm-up, and its notify-take loop re-checks ``s_ev_want_day`` before warming — a torn 64-bit read of the day cache is designed for (worst case: one extra re-bake).
   :align: center

.. graphviz:: ../_graphs/sky-events-scan.dot
   :alt: The day scan opened — full_moments' 6 h grid over the minus-45 to plus-15 day window, about 600 se_dot evals cached per calendar day, se_argmax refining each peak at plus-minus 6 h in 10 min steps with peaks gated S·E greater than 0.9, classify's five flags on F and F_prev, and the knob's 20 ms yield budget compiled into both scan loops
   :caption: The scan — ``full_moments`` (:138) sweeps a 6 h grid over [−45 d, +15 d], up to ~600 ``se_dot`` evaluations, cached per calendar day (:211); ``se_argmax`` (:101) refines each strict peak at ±6 h in 10 min steps (73 samples); a peak needs S·E > 0.9, F is the nearest one and F_prev the one before it (the blue test); the five flags ride ``classify`` (:221) — the ±18 h moment test, the shared-month blue test, super/micro MEASURED at F (357 500 / 405 500 km), BLOOD tested ALWAYS since an eclipse can start hours before the window opens. The knob has no worker, so the 20 ms wall-clock yield budget is compiled INTO both scan loops (:26-31, :101, :148) — the bench of 2026-11-11 proved a fixed every-32-evals yield still starves IDLE1.
   :align: center

.. graphviz:: ../_graphs/sky-events-consumers.dot
   :alt: The consumers opened — the event tint LUT composed over the warm base with the neutral gate LUT never tinted, the per-pixel eclipse bite riding the hour-centre bake, and the minute probe's 5-bit signature deciding the re-bake with the commit on show
   :caption: The three uses — every core-1 producer calls ``moon_gray565_set_event(&ev)`` before its bake (``moon_render.c`` :88), the event multipliers compose OVER the warm base and ``set_warm(0)`` wins over ``set_event``, so the neutral GATE LUT stays untinted and the image gate byte-identical; the bite is per-pixel lit Lambert × (1 − 0.85 × ``moon_event_occ``) — 1 fully in the umbra, 0 outside the penumbra, linear between — and it rides the hour-centre bake, so the hourly refresh tracks its drift across the disk by itself; the minute probe compares the 5-bit signature against ``s_baked_event`` — blood ON entering the shadow, OFF at exit — and commits only ON SHOW, so a cycle that never shows stays due and self-heals; when the hourly probe takes the hour, IT subsumes the re-bake.
   :align: center

Wakes: the deferred re-bake (live)
..................................

Waking from sleep (touch, tilt, or an alarm dismiss) shows the current
moon phase **static**, with no animation: when the day has not turned, the
boot's bake is still current, so the first wake frame is the current
phase, not a dark-side disk
(:doc:`the clock and sleep machine <clock-sleep>`). When the frame
*is* stale — a new day, a changed sky-event signature,
or a frame that was mid-bake when the device slept —
the wake no longer bakes on the LVGL task: the parked frame displays
immediately and ``moon_rebake_request()`` re-bakes on a core-1 task
(the same worker pattern as the day-tick re-bake and the hourly producer
— the spawn edges for all three of that request's callers, and the
wake-hand-back arrows they all share toward the tick, are the tick band
of :ref:`the crew sketch <task-roster-arch>`).
The finished frame is
never displayed off-task — the convert flushes only on the LVGL task
(the 3.5 KB stack rule) — so the bake merely raises a show flag and
the **next tick** displays it: invalidate, one atomic whole-panel
``lv_refr_now`` under ``s_rgb_mtx``, then the drain verify. A bake
aborted mid-frame marks the frame stale and is never published, so a
half-gathered disk never reaches the panel. The bake's ~1.3 s thus
left the wake path entirely: what
the user touches is the wake handler returning, not a bake.

The orbit (tilt viewport)
-------------------------

The home's moon is a static bake — and when the tilt sensor holds a
sustained off-axis pose, the **orbit** rotates the *viewport* itself
(the camera basis folds about the screen normal by the tilt angle) —
a re-bake at the tilted orbit (latest-wins: only the newest pending
orbit is baked; older tilt samples are dropped). The orbit bake is a
normal ``moon_bake`` into ``s_frame`` with the orbit-folded camera
basis, built row by row off the LVGL task so a cancel lands fast;
its completion is one atomic whole-frame flush — the tick's orbit
consumer caps its batch at ``MOON_ORBIT_BATCH`` = all strips, and the
publish is all-or-nothing, so the viewport lands as one ~150 ms
frame rather than a progressive top-to-bottom wipe.
The orbit is what makes "tilt the clock and the moon shows a
different face" true, at the cost of a 1.3 s bake per sustained
change (the tilt confirmation — 1 s windowed mean, integer
quadrant, sustained confirmation, :doc:`the clock and sleep machine <clock-sleep>` — exists precisely so a
single sample never pays that cost).

The dusk (removed)
------------------

The dusk spin (the light-sleep entry's pole rotation to the dark side)
is gone. The light-sleep entry is the **static dim** itself:
``moon_ui_pause(true)`` interrupts the orbit and pauses the tick, the
clock widget drops to ``UI_CLOCK_ALPHA_PCT`` (20 %),
``moon_ui_panel_dim_ramp`` walks the panel to ``UI_LIGHT_SLEEP_DIM_PCT``
(25 %), and the deep-sleep countdown is armed at the entry itself —
sleep depth follows the timer
(:doc:`the clock and sleep machine <clock-sleep>`). What the dusk did,
and the constant-projection property that made it cheap, is in :doc:`the
design history <../project/history>`. The bake entry point still carries
the ``dk_sup`` backlit-sliver parameter (the portable core is shared
verbatim with the host tools); every device bake passes 0, and :doc:`the
dark side <../science/darkside>` documents the model.

The fade (no pixels written)
----------------------------

.. raw:: html

   <video src="../_static/img/fade.mp4" controls muted playsinline width="400"></video>

The fade-to-black screen switch, filmed from the bake (``fade.mp4``, from the bake — :doc:`the build system <../reference/build-system>` regenerates it with ``docs/gen_media.py``).

Every screen switch (except the both-moon switch — airplane / WiFi /
the sleep screens share the one moon image) dims the **panel's own
hardware brightness** (0x51) to 0, switches the screen while the
panel is still black, and ramps back. The machine as a sealed
overview plus the two legs opened — the fade-out, and the three
branches of ``moon_ui_screen_switched`` including the starved-fade-in
rescue:

.. graphviz:: ../_graphs/screen-switch.dot
   :alt: The screen-switch fade machine sealed — loadScreen's both-moon decision, the self-guarded 0x51 fade-out ramp from the tracked level, the clock-widget reparent while black, and the three branches of moon_ui_screen_switched: the #34 starved-fade-in rescue (amber), the while-black atomic flush plus fade-in ramp, and the plain atomic switch
   :caption: The sealed view — ``loadScreen``'s both-moon test (``ui.c`` :51: fade every switch EXCEPT old ≠ new with both sides showing the moon — ``moon_ui_screen_is_moon`` walks ``s_moon_roots``, :3078 — because airplane / WiFi / the sleep screens share the one moon image: dimming it would be a fade with nothing to fade), then the self-guarded fade-out, the reparent while black, and the one branch taken (``moon_ui.c`` :3119).
   :align: center

.. graphviz:: ../_graphs/screen-switch-fade.dot
   :alt: The fade-out leg opened — the self-guard that paints plain without pending, the owed fade-in flagging the 0x51 writers' multiplex, the ramp starting from the tracked cent (25 % after the light-sleep dim), and the scan-storm rule that drops every step
   :caption: The fade-out leg — ``moon_ui_fade_out`` (:3019) is self-guarded (no moon, FADE ≤ 0, or baking: paint plain WITHOUT pending), but once it commits, the fade-in is OWED either way and ``s_fade_busy`` is the 0x51 writers' multiplex — the light-sleep dim queues behind it, because a 0x51 queued behind a strip that never completes would park its writer forever (the settings-entry freeze). The ramp starts at the TRACKED level ``s_panel_pct``, so after the light-sleep dim it fades from 25 %, and ``panel_hold_live()`` drops ALL steps, leaving the panel at its truthful tracked cent. The old overlay fade charged a full-screen ``lv_refr_now`` per step (~175 ms of QSPI strips each) and read as a top-to-bottom CRAWL — that is why the ramp replaced it.
   :align: center

.. graphviz:: ../_graphs/screen-switch-branches.dot
   :alt: The three branches opened — entry bookkeeping with the replay abort, the #34 starved fade-in rescue landing raw 100 percent under the panel mutex ignoring the hold, the fade-in leg's atomic flush while still black then the ramp to 100, and the plain atomic switch with its drain witness
   :caption: The three branches — entry bookkeeping first: the pipelines' display gate ``s_moon_screen``, and leaving the moon mid-replay ABORTS the replay; the #34 starved branch fires when the bake guard won the race after the fade-out RAMP LANDED (tracked 0) — a raw ``bsp_display_brightness_set(100)`` under ``s_panel_mtx`` IGNORING the radio hold (stranding a black panel with a live app outranks one 0x51 riding the storm bus; the P4 bench of 2026-11-13: pitch black, app fully alive), logged as ``fade-in starved by the bake guard``; the fade-in leg lands the new screen as ONE atomic whole-panel ``lv_refr_now`` while the panel is STILL BLACK (drain witness "switch fade-in") and only then ramps 0→100, so the fully rendered screen appears at once; the plain branch is the invariant's atomic switch under ``s_rgb_mtx``, witness "screen switch" — and the shared clock widget reparents onto the new root and refreshes from the RTC during the black window, so the wake's one frame shows the current minute.
   :align: center

.. code-block:: c
   :caption: components/ui/moon_ui.c (the fade ramp — shared by the screen switch and the light-sleep dim)

   static void fade_ramp(int from_pct, int to_pct, int ms) {
       if (panel_hold_live())
           return; /* storm: no write lands, so the panel stays at its tracked
                    * level -- s_panel_pct untouched is the truthful bookkeeping */
       for (int s = 1; s <= FADE_STEPS; s++) {
           int pct = from_pct + (to_pct - from_pct) * s / FADE_STEPS;
           xSemaphoreTake(s_panel_mtx, portMAX_DELAY);
           bsp_display_brightness_set(pct);
           xSemaphoreGive(s_panel_mtx);
           if (s < FADE_STEPS)
               vTaskDelay(pdMS_TO_TICKS(ms / FADE_STEPS));
       }
       s_panel_pct = to_pct; /* the steps are portMAX_DELAY: by the end the
                              * panel is unambiguously at the target */
   }

The screen switch and the light-sleep dim both call it with
``UI_SCREEN_SWITCH_FADE`` = 240 ms (the hourly refresh's own fade-to-
black legs are retired with the strip reveal — its old frame stays up the
whole bake instead); ``FADE_STEPS`` = 12, so 20 ms per step at the
default. Each step is one
~100 µs SPI param transaction — the ramp is free, so the steps are many
and short, and the luminance steps stay small enough not to read as
discrete steps. The fade-in
leg displays the new screen as **one atomic whole-panel
``lv_refr_now`` while the panel is still black** — the invariant's
atomic display, off the visible path — and then the brightness comes
up and the fully rendered screen appears at once.

The fade-in is owed, not guaranteed by the leg itself: when a bake
pipeline owns the buffers (the orbit / rebake / hourly own the branch
today),
``moon_ui_screen_switched`` takes its plain-paint early-return
branch. That branch rescues a starved fade-in — if the fade-out
ramp actually landed (tracked level at true 0), it brings the panel
back to 100 % under the panel mutex, ignoring the radio-traffic hold
(stranding a black panel with a live app outranks one 0x51 riding
the storm bus; the wake path makes the same write) — and logs
``fade-in starved by the bake guard``. The P4 bench caught the
un-rescued version on 2026-11-13: boot pitch black, app fully
alive.

The old overlay fade (a black rectangle composited over the screen,
stepped) charged one full-screen ``lv_refr_now`` per step — ~175 ms
of QSPI strip traffic each — and the panel's left-to-right strip fill
showed as a slow **crawl** on every switch. The 0x51 fade writes no
pixels at all; the panel scales everything on screen at once (the
moon, the labels, the arc) with no overlay to
composite. That is why the fade is the only sanctioned
exception to the atomic rule: the rule is preserved *off the visible
path*, not violated.

The light-sleep dim is the same ramp to a floor: the entry ramps
**down** to ``UI_LIGHT_SLEEP_DIM_PCT`` (production: 25%) over the
fade duration, and the wake / an alarm ring ramps it back to 100.
The frame under it is **static** — with the dusk retired there is
nothing moving at the dim level; the clock widget additionally drops
to ``UI_CLOCK_ALPHA_PCT`` (20 % alpha) and the seconds timer pauses
with the tick, so the sleeping display is one quiet still image.

Measured timings
----------------

The device-side numbers the *live* pipelines produce (full
benchmark context in :doc:`performance <../verification/performance>`):

.. list-table::
   :header-rows: 1
   :widths: 30 22 48

   * - Pipeline
     - S3 (466 px, 400 disk)
     - P4 (720 px, 650 disk)
   * - Static / deferred bake, full ``MOON_PX``
       (boot, wake, day tick, hourly miss, orbit)
     - ~1.3 s (864-albedo PSRAM load-latency floor;
       deferred re-bake / hourly show the next tick)
     - ~2.4 s (2048 albedo; bake median ~1543 ms @ 650 px)
   * - Hourly refresh, warm card (TF hour-cache fetch)
     - **measured 185–235 ms** whole cycle (read + decode +
       publish; ``new_moon_epoch_cached`` memoization, :commit:`dabbf475`
       forced-hourly bench) — no fade legs (the panel never dims
       since :commit:`ee4e3efd`; the hit frame lands atomically over the old
       one in the bright panel)
     - read ~55 ms + decode ~79 ms per hour (fetch path carried;
       P4 bench pending)
   * - Hourly refresh, cold card (bake + write-through store)
     - strip-loop bake internals **2687 ms**, whole cycle
       **3083 ms** (2026-11-14 bench; no fade legs owed since
       :commit:`ee4e3efd`, so the whole cycle is the bake + one atomic show +
       the store) — inside the 5 s budget; the old frame is up the
       whole cycle, the seconds arc sweeps the watermark
       (:doc:`the compute progress <compute-progress>`)
     - bake ~2.4 s; strip loop shared — P4 bench pending
   * - Boot replay, warm card (per-frame TF fetch + atomic show)
     - pacing floor ``UI_MOON_REPLAY_FPS`` (8);
       final strip-loop leg measured **2009–2615 ms**
       (400/400 rows, three boots 2026-11-14; the quick phase disc
       is up from boot meanwhile)
     - same shape (13-row strips); bench pending
   * - Screen-switch fade (0x51 ramp, no pixels)
     - 240 ms × 2 legs, 12 steps (~100 µs param each)
     - 240 ms × 2 legs, 12 steps
   * - Light-sleep dim (the same ramp to a floor)
     - one ``UI_SCREEN_SWITCH_FADE`` ramp down to 25 %;
       clock to 20 % alpha
     - same

The pattern worth internalizing (it holds for the bake paths that
remain): **everything expensive is built
once** (the relief cache at boot, the static frame at bake completion),
and the per-frame work is gather-class
(8–50 ms). The retired spin/sweep/dusk pipelines were display-paced,
not bake-paced, which is why they ran for minutes without the touch core
ever seeing a bake; their full per-frame tables are in the git history of
this page.

The clock digit roll (opt-in)
-----------------------------

The clock digits can animate a minute-pair change as a roller-style
roll (two-line strip, ease-out, ``CLOCK_ROLL_MS`` 300 ms, in the wheel-
delta direction). It is gated by ``UI_CLOCK_ROLL_ENABLE`` and **OFF by
default**: the roll fires exactly when the seconds arc completes its
cycle, and read as a spurious arc animation on the bench ("the arc
seems to have an animation active when completing the full cycle").
With it off, every pair change takes the mixed-jump hard-swap branch —
the label changes in one frame. The seconds arc itself never animates:
LVGL 9.6's ``lv_arc_set_value`` is instant and the arc styles carry no
transitions.

Next: the machine that decides *when* all of this runs — the clock,
the idle/sleep state machine, and the wake paths (:doc:`the clock and sleep machine <clock-sleep>`).
