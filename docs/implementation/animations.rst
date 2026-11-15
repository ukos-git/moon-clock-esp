The animation pipelines
=======================

A moon clock is mostly *waiting* — waiting for the next minute, the
next hour, the next wake. The project's answer to waiting used to be
to make the waiting itself the interface: three motion pipelines, all
built on
the one atomic-display rule (:doc:`the architecture <architecture>`) and the one panel-write
discipline (:doc:`the display pipeline <display>`). The user retired
all three on 2026-11-13. What is left on the home:

1. **The orbit** — a *response*, not an animation: the tilt viewport
   re-bake, one atomic frame per sustained pose change.
2. **The hourly refresh** — the moon redraws once per hour behind the
   hardware brightness fade: fade to black, produce the current-phase
   frame (a cache-hit frame shows atomically; a computed frame is
   *revealed* strip by strip as it bakes, the fade-back starting at
   the first completed strip — :doc:`the bake reveal <bake-reveal>`).
   The one live motion left on
   the home (see *The hourly refresh* below).
3. **The fade** — the screen-switch dim, which writes no pixels. Both
   the screen switch and the hourly refresh's two legs ride this ONE
   hardware-brightness-ramp exception to the atomic-display rule
   (:doc:`the architecture <architecture>`).

Retired 2026-11-13, one after the other: **the free spin** (the
ball-in-hand burn-in rotation — *"It does more things worse than
good."*), **the boot sweep** (the current synodic month played at
boot), and **the dusk** (the light-sleep pole spin to the dark side —
light sleep is a **static dim frame** now: panel to 25 %, clock to
20 % alpha, and the deep-sleep countdown armed at the entry itself;
:doc:`the clock and sleep machine <clock-sleep>`). The home sleeps as a
static dim and refreshes its moon once per hour (below). At boot it
**replays**: every hour frame the TF card holds plays ascending on the
boot black and stops on the newest, then the live-computed current
phase lands as the final frame — **revealed**: the frame draws itself
north to south at the actual bake rate instead of sitting black until
it is done (:doc:`the bake reveal <bake-reveal>`); with no card or an
empty cache that reveal is the first thing the panel shows at all
(*The boot replay*, below). This is
pure storage playback of the hour cache — not a return of the computed
boot animation the user retired; with ``UI_MOON_BOOT_REPLAY=n`` the
home boots straight to the static current phase again. The home's moon
is plain LVGL, shown static; see :doc:`the architecture <architecture>`
for what the removals left of the exceptions list. The removed
subsystems are summarized below as dated history (their full per-frame
measurements live in the git history of this page). The TF card cache
module outlived the sweep it was built for and now serves the hourly
refresh as the **TF hour cache** (below); its sweep-era Kconfig keys
(``UI_MOON_TFCACHE_FPS``, ``UI_MOON_TFCACHE_PREFETCH_MIN_SLOTS``) were
retired with the config sweep.

**Burn-in is accepted.** With the rotation gone, the S3's AMOLED shows
the moon in one place for weeks on end, so a faint burn-in of the moon
into the panel is *likely* over the device's life. As of 2026-11-14
that is a settled product decision, not an oversight: the user counts
a burned-in moon a **feature rather than a bug** — the ghost is the
clock's own subject, and it will likely not disturb reading the
display. Re-introducing any anti-burn-in machinery (a rotation, a
pixel nudge, anything) needs a fresh decision from the user first.

The remaining pipelines share one pipeline shape: a *bake task* (core 1) produces
frames off screen into the shared PSRAM buffers; the *tick* (LVGL
task, 50 ms) displays each published frame as **one atomic whole
frame**; backpressure (the bake waits for the display) keeps the two
from tearing a frame in half.

The hourly refresh (``UI_MOON_HOURLY_ENABLE``)
----------------------------------------------

The one live motion left on the home: once per hour the moon redraws
behind the hardware brightness fade. The user's brief was *"Update the
moon hourly with a fade to black, redraw and fade-in."* Only the panel's
brightness animates — the clock, the alarm row and the seconds keep
running through the whole cycle; the moon simply fades out, is rebuilt,
and fades back.

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
* **The tick legs** (``moon_hourly_service``, LVGL task) do the fade to
  black on the panel's OWN hardware brightness (``fade_ramp`` paced by
  ``UI_MOON_HOURLY_FADE_MS``, default = ``UI_SCREEN_SWITCH_FADE``) and
  spawn the producer. On a **TF fetch hit** the finished frame lands as
  **ONE atomic whole-panel flush** while the panel is still black (the
  fade's rule), then the brightness ramps back. On a **cache miss** the
  frame is instead *revealed* as it bakes (``UI_MOON_BAKE_REVEAL``,
  default y — :doc:`the bake reveal <bake-reveal>`): the square is
  cleared to black and each completed strip of the bake lands as one
  atomic band flush, with the fade-BACK paid at the first completed
  strip instead of after the whole bake. A producer that misses its 5 s
  budget (``HOURLY_BUDGET_MS``) leaves the OLD frame (a half-revealed
  square is healed whole first) and only restores the brightness —
  never a stranded black (a late producer still finishes its reveal and
  shows, with no second fade-in).
* **The producer** (``moon_hourly_task``, core 1, 12 KB, latest-wins)
  fills a PSRAM bounce: it asks the **TF hour cache** first (a hit is a
  JPEG decode instead of a ~1.3 s (S3) / ~2.4 s (P4) bake — and a hit
  never arms the reveal), else it bakes row-chunked with the
  rows-completed watermark published under ``s_rgb_mtx``
  (``moon_reveal_bake``, the same output as ``moon_bake``) with
  write-through store + eviction. It classifies the hour-centre instant
  once (the gray→RGB565 tint LUT + the blood-moon occlusion), then
  publishes ``s_frame`` + ``s_rgb`` in ONE ``s_rgb_mtx`` block. It never
  touches the panel.

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
       TICK->>PN: fade-to-black (0x51 ramp, writes no pixels)
       TICK->>P: spawn (latest-wins, 5 s budget)
       P->>TF: fetch the hour-centre key (hour*3600 + 1800)
       alt card hit
           TF-->>P: stored gray JPEG
           P->>P: tint LUT, publish s_frame + s_rgb in one s_rgb_mtx block
           TICK->>PN: ONE atomic whole-panel flush while still black, drain-witnessed
           TICK->>PN: fade back 0 to 100
       else card miss (bake reveal)
           P->>P: moon_reveal_bake row-chunked, rows watermark under s_rgb_mtx
           TICK->>PN: black canvas, then one atomic band flush per completed strip
           TICK->>PN: fade back paid at the FIRST completed strip
           P->>TF: write-through store + eviction (max of synodic-back and new-moon floor)
       else producer misses the 5 s budget
           TICK->>PN: heal the half-revealed square back to the OLD frame, restore brightness only
       end
       Note over MIN,PN: s_baked_hour + s_baked_event commit ON SHOW — a cycle that never shows stays due and self-heals on the next minute tick
       Note over TICK: a sleep entry mid-cycle drops the pending legs and restores the brightness before pausing (moon_ui_pause)
   </pre>

The bake key is the **hour centre** (``hour * 3600 + 1800``): stable
across boots and boards, so a card seeded by one boot serves the next
the byte-identical gray, and the on-screen frame is at most 30 min stale
at the show. ``s_baked_hour`` / ``s_baked_event`` commit *on show*, so a
cycle that never shows leaves the next hour still due (it self-heals on
the next minute tick).

Sleep hand-off. If a sleep entry lands mid-cycle, ``moon_ui_pause(true)``
drops the pending legs and restores the brightness before pausing the
tick — an armed-or-black hourly must never hand the sleep screens a black
panel (the screen-switch fade-starve lesson, this time on the sleep path).

**Bench hook.** ``CONFIG_TEST_HOURLY_FORCE_S=N`` fires the full cycle
every N seconds while the home is active and not idle — acceptance for
the fade/produce/show legs (cold card = miss + bake + store; warm card =
fetch), without waiting on a real hour boundary.

The TF hour cache (``UI_MOON_TFCACHE`` + ``UI_MOON_HOURLY_TFCACHE``)
.....................................................................

The cache persists one small gray JPEG per HOUR under
``/moon_cache/<calcver>_<target>_<px>/``; the folder name carries the
calculation version — the version the repo sets **manually via
commitizen** (``cz bump`` writes ``version = "x.y.z"`` into ``.cz.toml``,
and the root ``CMakeLists`` reads that line as ``MOON_CALC_VER``), falling
back to the ``git describe`` short hash only until the first bump — so a
deliberate version bump starts an empty folder while plain commits leave
the card alone, and
a self-describing ``meta.txt`` wipes/recreates the folder on any
resolution mismatch (``moon_tfcache.c``). Two rules bound the folder
(the user's cache policy, 2026-11-14): nothing more than one synodic
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
prunes at the crossing.

The producer mounts the card **lazily on the first producer to run** —
the boot replay task when ``UI_MOON_BOOT_REPLAY`` is on (a card-less
boot then pays ~1 s of mount probe before the revealed-now path starts),
otherwise the first hourly refresh, in which case boot never touches the
SD bus — and every card failure (missing card, failed mount, failed
decode, a mid-run pull, a store error) flips ``tfcache_ready()`` false
for the boot and the refresh silently bakes on-device (the card-less
path). The fetched frame runs through the SAME ``moon_gray565()`` tint
LUT as a baked frame, so cache and compute paths are pixel-identical by
construction.

The boot replay (``UI_MOON_BOOT_REPLAY`` + the TF keys)
.......................................................

The user's brief (2026-11-14): *"on bootup, during re-calculation of
the current moon, all displays should show all previously calculated
frames as an animation and stop at the last calculated frame that was
stored on the TF card… If no cache is available, the display stays
black until the current moon phase is calculated."*

It is the hourly split reused (``components/ui/moon_ui.c``, the
``replay`` section): a core-1 producer (``moon_boot_replay_task``,
12 KB, the hourly/rebake priority) enumerates the cached hours
ascending (``tfcache_list_hours``, the list in PSRAM), fetches each
gray JPEG, converts it through the neutral tint LUT (the stored grays
are event-independent; classifying 709 epochs would cost more than the
animation is worth) and publishes ``s_frame`` + ``s_rgb`` in ONE
``s_rgb_mtx`` block per frame — exactly the hourly publish shape. It
never touches the panel. The **tick** lands every published frame as
ONE atomic whole-panel ``lv_refr_now`` under ``s_rgb_mtx``, each show
**drain-witnessed** (up to ~709 shows per boot — witnessing only the
last one would let a lost completion pin the next ~2 s per strip).
No fade: the brightness never moves; the replay's black is the panel's
boot black. Pacing is a floor (``UI_MOON_REPLAY_FPS``, min dwell per
frame) plus a single in-flight frame (the producer waits for the tick
to consume the show flag; every wait is bounded so a paused tick can
never wedge it). ``UI_MOON_REPLAY_BUDGET_MS`` bounds the whole
animation — overrun stops the **animation**, never the moon: the
final leg still bakes and shows the current phase.

The **final leg** is the "during re-calculation" half: the current
phase is live-computed on the same task (classify + event LUT +
``moon_reveal_bake`` into the hourly bounce **outside** the mutex,
published in one short guarded block — the hourly shape again), commits
the bookkeeping (``s_orbit_epoch`` / ``s_baked_hour`` / ``s_baked_event``
/ ``s_baked_day``) and rides the tick's last atomic show, drain-witnessed
as ``boot replay final``. With ``UI_MOON_BAKE_REVEAL`` (default y) that
final bake is *revealed* while it runs — the square goes black and each
completed strip lands as one atomic band, so the current phase draws
itself north to south at the bake rate instead of the panel freezing on
the newest stored frame for the ~1.3 s (:doc:`the bake reveal
<bake-reveal>`). Stored frames never move
``s_orbit_epoch`` — they are historic phases; only the final bake may.
No card, cold folder, version wipe or no clock: the animation leg
simply has nothing to show and the panel holds its boot black until the
final leg starts revealing (or, without a trustworthy time, until the
day-tick re-bakes after the clock syncs — the pre-replay black path,
which does *not* reveal). The
**knob** has no SD socket, so it always takes the black path — but not
a silent one: its boot bake is revealed row by row through its own
``KNOB_UI_BAKE_REVEAL`` seam (:doc:`the knob display <knob-display>`).
Demo mode boots
without the replay (its scripted tour owns the display).

The whole presentation, including the abort path and the single-caller
arbitration the ladder rests on:

.. raw:: html

   <pre class="mermaid">
   flowchart TB
     BOOT["Boot, UI_MOON_BOOT_REPLAY on, not demo"] --> HOLD["idle ladder held at spawn"]
     HOLD --> LIST{"card mounted and hours listed (lazy first-producer mount)"}
     LIST -- "hours ascending" --> FETCH["per hour — fetch gray JPEG, neutral tint LUT, publish under s_rgb_mtx"]
     FETCH --> SHOW["tick lands each frame as ONE atomic whole-panel flush, drain-witnessed"]
     SHOW --> MORE{"more hours?"}
     MORE -- "yes" --> FETCH
     MORE -- "no, stopped on newest" --> FINAL
     LIST -- "no card, cold folder, version wipe, no clock" --> BLACK["panel holds boot black"]
     BLACK --> FINAL
     FINAL["final leg — classify + event LUT + revealed now-bake outside the mutex"] --> LAST["tick's last atomic show, bookkeeping commits, end leg re-arms the idle ladder"]
     SHOW -. "sleep entry or screen switch aborts — un-shown frames dropped, final bake skipped" .-> ABORT["s_baked_day stays 0 — the minute day-tick queues the corrective re-bake on return"]
   </pre>

While ``moon_ui_replaying()`` is true the hourly probe/spawn, the
day-tick and the orbit gate out and every idle re-arm site in
``actions.c`` defers — the two core-1 producers (replay, hourly) never
run at once, which is what keeps tfcache's single-caller contract
intact.

Lifecycle holds (the boot presentation owns the screen for the length
of the animation): the replay disables the idle→sleep ladder at spawn
and the tick's **end leg** re-arms it when the presentation is over
(every idle re-arm site in ``actions.c`` — boot wake, WiFi-sync
hand-off, airplane switch, the test hooks — defers while
``moon_ui_replaying()``); the hourly probe/spawn, the day-tick and the
orbit gate on the same signal, so no fade cycle, corrective re-bake or
orbit peek races the replay over ``s_frame`` / ``s_rgb`` / the card
(tfcache's single-caller contract holds: the two producers never run
at once). A sleep entry (``moon_ui_pause(true)``) or a screen switch
away from the moon **aborts** the animation — the un-shown frames are
dropped and the final bake is skipped (``s_baked_day`` stays 0, so the
minute day-tick queues the corrective re-bake once the device is back
on the home).

The boot sweep (retired 2026-11-13)
-----------------------------------

Retired with the rest of the animation stack: the COMPUTED boot
animation is gone — no per-frame bake task, no 1344-frame montage. With
``UI_MOON_BOOT_REPLAY=n``, ``moon_ui_start`` bakes the CURRENT phase
directly and boot is one static frame, like every wake. What returned
on 2026-11-14 is a different animal: the boot replay (*above*) plays
**stored** hour frames — pure playback, one JPEG decode per frame, no
bake — and ends on the newest stored hour plus the computed now-frame,
not on a computed montage endpoint. ``new_moon_epoch`` lives again in
that service (the cache policy's new-moon floor, not a sweep span); the
sweep itself is gone. The knob companion followed on 2026-11-14,
retiring its own cold-boot sweep, and with no SD socket it boots to the
single static frame — its first bake revealed row by row over the boot
black (:doc:`the bake reveal <bake-reveal>`, :doc:`the knob
display <knob-display>`). The subsystem as shipped is
summarized here only as dated history; the sweep code
(``moon_sweep_task`` / ``moon_sweep_interrupt`` / ``s_sweep_*``) and its
Kconfig (``UI_MOON_ANIM_*``) are gone from the clock tree.

What it did: at boot the moon replayed the **current synodic cycle** —
from the last new moon (a dark disk) to *now*, at most 29.53 days — in
``UI_MOON_ANIM_FRAMES`` frames (1344 since 2026-11-07, when the per-frame
illumination jump at the old 336-frame density read as a visible stutter).
The span came from ``new_moon_epoch``, a two-pass backward scan of
``moon_geometry``'s illumination (a 6 h coarse grid over one synodic month,
then a 20-min fine pass around the coarse minimum, the 1e-6 slack keeping a
tied coarse step from drifting into a neighbouring cycle). A core-1 bake
task produced the frames **off screen** and published them one at a time;
the tick displayed each as one atomic whole frame (the whole-panel
``lv_refr_now``, measured 304 ms S3 / 207 ms P4), and the **backpressure**
rule — the bake did not start frame *f*+1 until the tick had displayed
frame *f* — is what kept a mixed (torn) frame off screen. Because the
display was faster than the bake (~1.3 s at native resolution), the bake
paced the sweep: a full-res boot ran ~36.5 min (S3, 1344 frames) / ~40 min
(P4), intermediate frames baked at native ``MOON_PX`` (400 / 650) since
8954e88, so the montage was pixel-identical to the static bake. The
per-frame cost carried over unchanged to the deferred re-bake and the
hourly producer, which bake at the same native resolution and hit the same
864-albedo PSRAM load-latency floor (:doc:`the C core <core-c>`); the
retired subsystem's full per-resolution bake table and the
TF-cache-state × board wall table are the 2026-11-08 bench captures in this
page's git history.

The TF card cache it drove was the **sweep's** cache — its prefetch task
(``moon_tfprefetch``, core 0), its replay pacing (``UI_MOON_TFCACHE_FPS``)
and PSRAM slot floor (``UI_MOON_TFCACHE_PREFETCH_MIN_SLOTS``) retired with
the sweep, and the config sweep deleted those keys. The *module* outlived
it: ``moon_tfcache.c`` now serves the hourly refresh as the **TF hour
cache** (*The TF hour cache*, above) — the same versioned, self-describing
``/sdcard/moon_cache/<calcver>_<target>_<px>/`` folder, the same gray-JPEG
codec (``esp_new_jpeg``, format self-probed at init), the same
tint-LUT-identical fetch, and the same silent card-less fallback, all on
the per-board mount (S3 SPI 1-bit on SPI3_HOST, P4 4-bit SDMMC) and
``CONFIG_FATFS_LFN_HEAP``. Two dated facts about that module stay worth
knowing because they still shape it: the folder is keyed on the
commitizen-set version (the ``version`` line in ``.cz.toml``, which
``cz bump`` writes; the ``git describe`` short hash keys it only until the
first bump), so **the card re-keys on a deliberate version bump** and the
stale sibling folders are pruned then — plain commits no longer start the
cache from zero (the old per-commit ``git describe`` key did, ``-dirty``
suffix and all, until 2026-11-15); and the
decode out-buffer is allocated **lazily** on the first hit rather than at
init (task #38, 2026-11-13) — holding the ``3·S²`` buffer eagerly starved
the 8 MB S3's slot floor and silently killed the card path *every* boot, so
the cache could never warm. The spin's keep-mask/snapshot starvation that a
healthy card once exposed (task #39) has no surface left to starve — the
spin and its full-screen ``lv_snapshot`` are gone
(:doc:`the architecture <architecture>`). The once-a-day sky-events day
scan that used to print ``IDLE0`` watchdog bursts on the LVGL task at boot
was root-caused and moved off it 2026-11-09 (the core-1 sky-events worker,
:doc:`the clock and sleep machine <clock-sleep>`).

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
(the same worker pattern as the day-tick re-bake and the hourly producer).
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
a re-bake at the tilted orbit, displayed strip by strip
(latest-wins: only the newest pending orbit is baked; older tilt
samples are dropped). The orbit bake is a normal ``moon_bake`` into
``s_frame`` with the orbit-folded camera basis; its completion is one
atomic whole-frame flush.
The orbit is what makes "tilt the clock and the moon shows a
different face" true, at the cost of a 1.3 s bake per sustained
change (the tilt confirmation — 1 s windowed mean, integer
quadrant, sustained confirmation, :doc:`the clock and sleep machine <clock-sleep>` — exists precisely so a
single sample never pays that cost).

The dusk (retired 2026-11-13)
-----------------------------

Retired with the rest of the animation stack: the light-sleep entry is
now the **static dim** itself — ``moon_ui_pause(true)`` interrupts the
orbit and pauses the tick, the clock widget drops to
``UI_CLOCK_ALPHA_PCT`` (20 %), ``moon_ui_panel_dim_ramp`` walks the
panel to ``UI_LIGHT_SLEEP_DIM_PCT`` (25 %), and the deep-sleep countdown
is armed at the entry itself (the countdown used to fire only once the
dusk's dark side was on screen — sleep depth followed the animation; it
now follows the timer, :doc:`the clock and sleep machine <clock-sleep>`).
The code (``moon_dusk_task`` / ``moon_ui_dusk_start`` / ``MOON_DUSK_*``
and the shade-map decls) is gone from the clock tree.

What it did: the entry rotated the camera about the Moon's **pole**
(body-frame +z) at the current phase, 1°/frame along the shortest arc to
the fully unilluminated side (14–95 s, display-paced at ~0.2 s/frame),
then armed the countdown. It was cheap because of the
**constant-projection property**
(:doc:`the dark side <../science/darkside>`, :doc:`the C core <core-c>`):
a pixel's color depends on the body-frame surface direction and the
epoch's fixed Sun only — the view basis selects *which* direction is
visible but never enters the *color* — so the full body-frame shade map
was **built once** for the epoch (``moon_shade_map_build``, ~1.6 s S3 /
~282 ms P4) and every frame **re-projected** it through the folded camera
basis (a ``moon_rotate``-class gather, ~50 ms, **not** a ~1.3 s re-bake).
A fixed surface point kept exactly its map cell's color on every frame, so
the terminator could not flicker. The endpoint is where
:math:`\mathbf{E}\cdot\mathbf{S}` is minimized (the disk fully night-side;
the shortest-arc minimum :math:`\phi^* = \mathrm{atan2}(-B,\,-A)`,
host-verified against a full-circle grid), and with :math:`\mathbf{E}` the
orbit-folded view direction (the tilt peeks the view, so the endpoint was
computed from the folded, not the plain, :math:`\mathbf{E}`). The **final
frame** was one full-``MOON_PX`` bake at ``phi*``; its thin sun-lit rim was
handled by the ``dk_sup`` backlit-sliver parameter — the bake entry point
still carries it (the portable core is shared verbatim with the host tools)
but every device bake today passes 0, and :doc:`the dark side
<../science/darkside>` documents the model. The shade-map and pole-rotate
entry points stay in the portable core, exercised by the shared host tools
(:doc:`the C core <core-c>`).

The fade (no pixels written)
----------------------------

.. raw:: html

   <video src="../_static/img/fade.mp4" controls muted playsinline width="400"></video>

   The fade-to-black screen switch, filmed from the bake (``fade.mp4``, from the bake — :doc:`the build system <../reference/build-system>` regenerates it with ``docs/gen_media.py``).

Every screen switch (except the both-moon switch — airplane / WiFi /
the sleep screens share the one moon image) dims the **panel's own
hardware brightness** (0x51) to 0, switches the screen while the
panel is still black, and ramps back:

.. code-block:: c
   :caption: components/ui/moon_ui.c (the fade ramp — shared by the screen switch and the hourly refresh's legs)

   static void fade_ramp(int from_pct, int to_pct, int ms) {
       if (panel_hold_live())
           return; /* storm: no write lands, so the panel stays at its
                    * tracked level -- s_panel_pct untouched is truthful */
       for (int s = 1; s <= FADE_STEPS; s++) {
           int pct = from_pct + (to_pct - from_pct) * s / FADE_STEPS;
           xSemaphoreTake(s_panel_mtx, portMAX_DELAY);
           bsp_display_brightness_set(pct);
           xSemaphoreGive(s_panel_mtx);
           if (s < FADE_STEPS)
               vTaskDelay(pdMS_TO_TICKS(ms / FADE_STEPS));
       }
       s_panel_pct = to_pct;
   }

The screen switch calls it with ``UI_SCREEN_SWITCH_FADE`` = 240 ms, the
hourly refresh's legs with ``UI_MOON_HOURLY_FADE_MS`` (default the same);
``FADE_STEPS`` = 12, so 20 ms per step at the default. Each step is one
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
       publish; ``new_moon_epoch_cached`` memoization, 2026-11-14
       forced-hourly bench) + the two 240 ms fade legs
     - read ~55 ms + decode ~79 ms per hour + the two
       fade legs (fetch path carried; P4 bench pending)
   * - Hourly refresh, cold card (bake + write-through store)
     - revealed bake internals **2687 ms**, whole cycle
       **3083 ms** — inside the 5 s budget (+ store); the fade-back
       is paid at the first strip (observed: ``first strip up, panel
       brightness 0 -> 100``), so the dark window is the fade, not
       the bake (:doc:`the bake reveal <bake-reveal>`)
     - bake ~2.4 s; reveal shape shared — P4 bench pending
   * - Boot replay, warm card (per-frame TF fetch + atomic show)
     - pacing floor ``UI_MOON_REPLAY_FPS`` (8);
       final revealed leg measured **2009–2615 ms**
       (400/400 rows, 20 band states, three boots 2026-11-14)
     - same shape (26-row bands); bench pending
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

Task #54's split-flap look animated a minute-pair change as a
roller-style roll (two-line strip, ease-out, ``CLOCK_ROLL_MS`` 300 ms,
in the wheel-delta direction — the 5456799-era shared home). It is
gated by ``UI_CLOCK_ROLL_ENABLE`` and **OFF by default**: the roll
fires exactly when the seconds arc completes its cycle, and read as a
spurious ARC animation (user 2026-11-13: "the arc seems to have an
animation active when completing the full cycle to update top-to
bottom the spinner. If this can be switched off, that would be
good"). With it off, every pair change takes the mixed-jump
hard-swap branch — the label changes in one frame. (The shared
``label_changed`` tail that used to rebuild the spin keep-mask on
this signal died with the spin subsystem, 2026-11-13.) (The seconds
arc itself never
animated: LVGL 9.6's ``lv_arc_set_value`` is instant and the arc
styles carry no transitions.)

Next: the machine that decides *when* all of this runs — the clock,
the idle/sleep state machine, and the wake paths (:doc:`the clock and sleep machine <clock-sleep>`).
