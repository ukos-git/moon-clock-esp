On-device demo tour and recording
=================================

"Every device looks the same and every function works" is only a claim
until someone films the three boards side by side and reads their
serial lines against the same clock. The **demo mode** is the scripted
tour that makes that comparison repeatable (Kconfig on both clocks and
the knob), and ``bench/record_demo.py`` is the harness that records it:
the C920 camera plus one timestamped serial log per board, cut into
per-phase comparison frames by the tour's own ``phase=`` markers.

Enabling it
-----------

.. list-table::
   :header-rows: 1
   :widths: 34 12 54

   * - Key
     - Default
     - Scope
   * - ``UI_DEMO_MODE``
     - ``n``
     - the clocks' scripted tour (S3 and P4, one build each);
       ``n`` builds the production binary unchanged
   * - ``UI_DEMO_FULL_TOUR``
     - ``y``
     - ``n`` keeps the plain MM:SS + five-moon-screen rotation (the
       original showcase)
   * - ``UI_DEMO_SCREEN_SEC``
     - ``15``
     - seconds per moon screen and per settings dwell
   * - ``KNOB_DEMO_MODE`` / ``KNOB_DEMO_PHASE_SEC``
     - ``n`` / ``15``
     - the knob's canonical-phase walk (full tables:
       :doc:`UI Kconfig <../reference/kconfig-ui>`,
       :doc:`knob Kconfig <../reference/kconfig-knob>`)

One cycle of the clock tour takes roughly seven minutes. (It used to
arm only once ``moon_ui_sweeping()`` was false — the boot sweep that
gate waited for retired 2026-11-13, so the tour now arms on schedule;
takes no longer span a boot sweep on the clocks.) Demo mode also
boots **without the TF boot replay** (``UI_MOON_BOOT_REPLAY`` is
compiled out of the boot path while ``UI_DEMO_MODE`` is on): the
scripted tour owns the display, and the moon starts static like with
the replay off.

The clock tour
--------------

Every tap of the tour goes through the **real touch path**: demo.c
creates a second LVGL POINTER indev (the BSP's real indev keeps
working for the human; the alarm/orientation indev callbacks were
bound to the real device before ``ui_demo_init`` runs) and feeds it
press/release pairs from a 10 ms engine timer. Hit-testing, the wheel's
sticky-sector reveal and every ``LV_EVENT_CLICKED`` handler therefore
execute for real — the tour verifies the touch entry, not a
loadScreen shortcut.

.. list-table::
   :header-rows: 1
   :widths: 16 44 40

   * - Phase
     - What the tour does
     - What it exercises
   * - ``moons``
     - the five moon screens on a ``UI_DEMO_SCREEN_SEC`` rotation,
       clock widget on MM:SS (per-digit odometer)
     - the shared clock widget, the atomic screen switches, the
       static current phase on every moon screen
   * - ``hub_walk``
     - a real tap opens the settings hub from home; every ring icon,
       sub-screen dwell, bottom-centre exit — including the alarm
       screen's pomodoro and real-alarm entries
     - the hub ring hit boxes, every settings entry/exit handler,
       the roller screens' entry refreshes
   * - ``wifi``
     - the hub's WiFi icon → the scan list → the first AP row (a
       prefilled fallback when no APs answer)
     - the background scan, row hit-testing, the SSID prefill
   * - ``wheel``
     - types ``demo``, tours all four registers (ABC + ``A``, 123 +
       ``1``, back to abc), centre backspaces ×2, then DISCARD (clear)
       and DISCARD again (exit)
     - the sweep press/release commit-on-release, the mode arc, the
       centre backspace (``SHORT_CLICKED`` at the 90 ms tap), the
       discard ladder
   * - ``back``
     - the WiFi screen's exit → the hub's central exit → home
     - the two exit handler levels
   * - ``roll``
     - one run of the synthetic tilt script (``action_roll_test_begin``)
       through the real orientation pipeline
     - the int4 quantization, the 1 s window, the 90-degree steps,
       the PSRAM transform layer + centre pivot
   * - ``sync``
     - one bounded ``action_start_wifi_sync()``, claimed via
       ``action_sync_demo_done`` (45 s budget), land on home
     - the transient WiFi screen, the background handshake, the
       airplane hand-off
   * - ``alarm``
     - the real ring script: fire, stop after the volume ramp,
       re-fire (the codec RE-OPEN), real dismiss through the wake
       path; then the wake's on-demand sync is drained to home
     - the alarm screen, the ES8311 open/ramp/fade/close/re-open
       cycle (audible on both boards), the wake flow

The alarm runs **last** in the cycle on purpose. The 2026-11-08 bench
take showed the S3's QSPI completion-loss race (lost DMA completion ->
``PANEL IO STUCK`` -> panel watchdog controlled reboot) firing ~29 s
into this phase in all four cycles the board ran — the alarm's
wake/dismiss window,
where the 0x51 hardware brightness ramp, the fade-to-black switch and
the spin resume (the spin retired 2026-11-13) then overlapped (the
same family as the #96 settings-freeze
stall). Placing the phase at the cycle end keeps roll and sync
comparable on every cycle even while that watchdog recovery runs; the
race itself was root-caused 2026-11-12 to the wake path's WiFi-init
storm and closed with the sync-storm panel-traffic hold
(:ref:`display-lost-dma` — see the closeout below).

Tap-point geometry: the screen roots carry the base orientation
(``UI_ORI_BASE_V10`` — 180° on the S3, 90° on the P4) and the roll
steps, always multiples of 90° about the panel centre. A widget's AABB
centre is therefore rotation-safe for ordinary taps; the wheel's
annulus/sector points (which live in the wheel's LOCAL space, the
space its handlers' inverse transform expects) are mapped into display
coords through the wheel screen root's own
``lv_obj_transform_point`` matrix.

Two things are deliberately **never tapped**: the wheel's circle-CHECK
commit key (it would write ``spiffs/wifi_credentials.txt`` and start a
connect) and the alarm screen's deactivate-all master (it would really
disarm the user's alarms). The tour's edits are bounded to the
pomodoro/real-alarm entry screens, which only re-seed their rollers'
current values.

The whole cycle as one picture — ``demo.c``'s phase machine, its two
safety rails, and the action-layer gates beneath it:

.. graphviz:: ../_graphs/demo-tour.dot

The taps themselves ride a **second POINTER indev** — a real
hit-tested injection beside the BSP's (press 90 + release 60 ms,
created after the alarm/orientation/idle registrations so it never
steals their target); see
:ref:`input-chain-ui <input-chain-ui>`.

Suppression while the tour runs
--------------------------------

The tour owns the display until reset; anything that would hijack it
is refused at the action layer (``components/ui/actions.c``):

* the **deep-sleep transition** returns early (and disarms the source
  countdown) while ``action_demo_active()``;
* **scheduled alarms** are dropped in ``action_alarm_triggered`` —
  the alarm phase opens this with ``action_demo_allow_alarm(true)``
  while it deliberately rings;
* the **hourly on-demand sync** and the **sync-complete hand-off** are
  skipped (the flag stays unclaimed; the tour claims it with
  ``action_sync_demo_done`` when it wants the result);
* the idle monitor is switched off at ARM and the idle timer is held
  every second, so no sleep countdown ever expires mid-take.

The knob tour
-------------

With ``KNOB_DEMO_MODE`` the producer task (``knob/components/knob_ui``)
replaces the day-tick loop after the boot presentation: eight
canonical phases of the current synodic month (epochs pinned at k/8
of the month), then a ninth step on the LIVE current phase (re-baked
every cycle — and re-reading *now* at the step itself, events on).
The canonical eight are **knob-only coverage**: the clocks have no
canonical-epoch display, so those steps have no same-name partner
step to be montaged against — what they verify is that the knob's
bake path renders every phase correctly (and the core is
view-independent, so all three boards *would* agree). The honest
cross-device pair is the **knob's live-now step vs the clocks' home
step**: both show the current phase baked from the live instant by
the shared core, and the harness folds the knob's ``phase=now``
marker into the ``home`` montage group (``PHASE_ALIASES``). The
caption carries the phase name (the now step shows the glass clock);
the power ladder stays held, so the knob never dims or deep-sleeps
during a take.

The serial contract
-------------------

Every phase entry logs, on all three devices:

.. code-block:: text

   I (123456) DEMO: phase=hub_walk cycle=2 screen=6

The harness parses exactly this line; ``cycle`` lets a long take
contain several passes and the montages group by ``(cycle, phase)``.

The recording harness
----------------------

.. code-block:: text

   source ~/esp/esp-idf/export.sh          # pyserial lives in the IDF env
   python3 bench/record_demo.py --minutes 30 --boot-pulse

* **Board identity from udev, never a hard-coded port** (the mapping
  follows the USB topology): ``1a86:55d3`` is the P4's CH9102,
  ``303a:1001`` with the bench MACs is the S3 / knob; ``--map PORT=BOARD``
  overrides. A sleeping knob has no node until the dial is turned.
* **Passive serial**: ``dtr``/``rts`` are held low BEFORE ``open()`` —
  a naive open of an Espressif USB-JTAG node cold-boots the board.
  ``--boot-pulse`` pulses RTS deliberately (the EN reset), so the
  boards boot in the camera's first second.
* **Camera**: raw ``yuyv422`` 1080p straight through x264 — the
  C920's own MJPEG encoder is the artifact source. At 1080p the raw
  YUYV stream tops out at 5 fps over USB 2 (measured: exactly 30
  frames in 6 s whatever rate is requested); resolution wins over
  frame rate for panel comparison, and every demo dwell is seconds
  long, so 200 ms granularity loses nothing.
* **Post-processing**: per phase event, one PNG per board extracted
  from the video at the marker + ``--lead`` seconds, then an
  ``hstack`` montage per ``(cycle, phase)`` under ``compare/`` — the
  side-by-side evidence. Output tree: ``video.mp4``, ``logs/<board>.log``
  (host-timestamped), ``frames/``, ``compare/``, ``timeline.csv``,
  ``meta.json`` under ``bench/recordings/<stamp>/``.
* Preflight refuses to run while ``fuser`` shows another session
  holding a port (parallel peer sessions are the usual cause).

Reading a take
--------------

* The **sweep** (retired from the clocks 2026-11-13, the knob's
  cold-boot sweep 2026-11-14) played the 1344-frame spec (693 frames
  at the current cycle age) from the last new moon on every device.
  With a warm TF cache it replayed (measured 2026-11-08: S3 295 s at
  400 px, ~2.3 fps; P4 217 s at 650 px, ~3.2 fps); cold it baked
  (~1.5 s/frame: ≈ 17 min on the S3, ≈ 1120 s ≈ 19 min on the P4):
  identical frame pacing and identical illumination curves were the
  first look-test.
* ``home`` (with the knob's ``now`` step aliased into it) compares
  the current-phase rendering across the three panels — same core,
  same live instant; the knob's canonical phase steps are its own
  bake-path coverage (the clocks cannot display a canonical epoch).
* ``alarm`` is audible in the video's audio track (both clocks carry
  the ES8311).
* The logs beside each take are read like any
  :doc:`device-verification capture <device-verify>`: progress lines
  present, timing sane, zero failure-class lines (no WDT fires that
  change behaviour, no ``PANEL IO STUCK``, no ``ESP_ERR_NO_MEM``).

Bench status (2026-11-08)
-------------------------

The first formal take (45 min, both clocks, ``--boot-pulse``, 1080p5;
the boards ran a binary predating the 900 ms tap settle and the
alarm-last order) produced:

* Both boards swept 693/693 frames from the warm TF cache (S3 295 s,
  P4 217 s) and armed 78 s apart. The tour walked the script
  identically on both panels: hub walk 16 tap steps on the S3 and 18
  on the P4 (the CLOCK icon), the WiFi scan returned 2 APs on the S3
  and 3 on the P4 (radio visibility) with row 0 tapped on each, the
  wheel typed ``demo``/``A``/``1`` plus backspaces and ran the
  discard ladder. The P4 completed six full cycles without a single
  reboot.
* Two tour bugs surfaced from the take's own logs. A stale wheel
  end-check (expected the pre-discard entry length) is fixed in the
  tree. The tap settle went from 350 to 900 ms, which stopped
  mid-fade verify on the plain screens but not on the settings
  screens: the re-take still logs ~18 (S3) / ~22 (P4) ``loadScreen``
  fallbacks per cycle, all in the hub walk and all the "one screen
  behind" signature — the 720² fade and the roller re-seeds outlast
  even 900 ms. Every one recovers (the fallback loads the expected
  screen before the next tap; no mistap observed), so this is log
  noise plus one redundant atomic flush per step; poll-until-settled
  verify is the agreed fix, kept as a tuning item.
* **Reproduced in the field** on the S3: ``PANEL IO STUCK`` fired in
  all four cycles the board ran, each time ~29 s after ``phase=alarm``
  (device ticks 668.7-673.8 s against alarm entries at 642.7-644.8 s
  — the tour is scripted from boot, so this is a fixed post-boot
  window); each time the panel watchdog rebooted the board
  (``rst:0xc`` RTC_SW_CPU_RST, no panic), back up in ~13 s plus the
  warm re-sweep. That is why the
  alarm phase runs last now; the race itself (the wake/dismiss
  window: 0x51 brightness ramp + fade-to-black + spin resume, the
  same family as the #96 settings-freeze stall) is an open follow-up.
* The alarm-last re-take (14:16, 28 min, warm caches) confirmed the
  containment **and** moved the stall: P4 ran three full cycles
  ending ``sync`` -> ``alarm`` with no reboot, and S3 completed
  ``dusk`` and ``dark`` every cycle — while the stall fired 29.7 s /
  30.6 s after ``dusk`` entry instead of ~29 s after ``alarm`` entry.
  Six for six it lands ~30 s into a panel-ramp window; the shared
  ingredient of the alarm-ring and dusk windows is the 0x51 hardware
  brightness ramp concurrent with panel flushes (dusk dims
  100 -> 25 %, the alarm wake ramps back up) — the #96 family, and
  the concrete lead for the follow-up. Cycle-0 montages cover moons,
  hub_walk, wifi, wheel, back, dusk, dark and home.
* Known noise, non-fatal: intermittent ``task_wdt`` IDLE0-starvation
  bursts on the S3 during the boot sweep. Zero ``ESP_ERR_NO_MEM``
  anywhere, zero WDT lines on the P4.
* The knob was not on the bench for this take (a sleeping knob has no
  serial node until the dial is turned); it is build-gated with the
  clocks.

**Follow-up closed (2026-11-12)** — the six-for-six stall's missing
ingredient was not the 0x51 ramp itself but the wake path's on-demand
WiFi sync. Bench hammer (``TEST_AUTO_WAKE_SEC`` auto-wakes 5 s after
the DEEP rung, 5-frame sweep, connected AP): every tilt wake from deep
sleep stalled — wake at *t*, ``PANEL IO STUCK`` at wake+4.0…6.3 s,
each time the panel watchdog's controlled restart (``rst:0xc``
RTC_SW_CPU_RST, no panic). The racing writer: the wake's sync task
runs ``esp_wifi_init`` — full Wi-Fi firmware load into RAM plus RF
calibration on the prio-23 driver task (wake → "WiFi initialization
finished" ≈ 1.2 s) — while the resumed spin's strips and the 10 ms
LVGL tick flushes are live on the panel bus; the storm takes the
completion of an in-flight QSPI DMA. All three corollaries held: boot
runs the identical storm *before* LVGL exists — always clean; the
alarm wake with the moon paused — clean; and the 0x51-rides-alone
guard on the brightness API (22c87ad) changed nothing — the flush
traffic, not the 0x51, was the racing writer. The fix is the
**sync-storm panel-traffic hold** (:ref:`display-lost-dma`): armed at
sync kickoff (quiescing the bus first), every panel write *dropped*
(never blocked) until the sync task has taken the radio fully down,
15 s self-healing cap, 4 s radio tail, whole-screen repaint on
release. The hammer then caught two more thieves in the same window —
the still-associated station's ~300 ms DTIM beacon wake stealing
completions after the release (fixed: the sync task itself
deinitialises the radio before releasing the hold, boot likewise
airplanes before the panel wakes), and LWIP's DHCP RELEASE TX-ing
through the softmac that ``esp_wifi_deinit`` was freeing (fixed: the
DHCP client is stopped while the radio is still up, then one 100 ms
settle before the free) — plus the per-cycle event-handler leak whose
DMA-block fragmentation had made the 10th ``esp_wifi_init`` NO_MEM
into an ``ESP_ERROR_CHECK`` abort (fixed: unregister on every deinit,
log-and-return instead of aborting). Final hammer proof (2026-11-12):
**ten consecutive DEEP→wake cycles, zero** ``PANEL IO STUCK``, WDT or
panic lines; the largest DMA block settles at a stable 15,360 B floor
from wake 4 onward.

**Follow-up closed (2026-11-12, same day) — the settings WiFi scan is
the same class.** User report: after leaving the WiFi password screen
the home clock row is blank white, no digits (the earlier phrasing was
"scrambled watch display after returning from settings"). New headless
repro hook ``TEST_SETTINGS_RETURN_SEC`` (writes nothing): open the
WiFi settings screen N seconds after boot, dwell 2 s, exit home —
dwell < the scan's ~3.9 s puts the home switch's atomic flush INSIDE
the running scan (the user's path: password → list re-entry restarts
the scan → exit home). Pre-fix capture: 400 ``co5300`` write failures
in ~3 s plus two silently dropped strips over exactly the clock row
(y 176..214, y 214..252) — the drops are permanent (LVGL answered
ready, nothing re-invalidated, and the spin's direct write only covers
the moon window). Fix: the scan session borrows the sync-storm hold
through ``moon_ui_panel_traffic_hold_ext`` (45 s cap), the release
owned by the GENERIC UI tick's ``screens_wifi_scan_hold_poll`` — the
hold follows the SCAN, not the screen (leaving mid-scan must not cut
the guard) — with the proven 4 s teardown tail; the release's
whole-screen repaint heals the storm's drops
(:ref:`display-lost-dma`). Post-fix captures: the natural exit (dwell
> scan) and the hard mid-scan exit both run ON → scan done → release
stamped 4000 ms → OFF with repaint queued — **zero** ``co5300``, zero
dropped strips, no WDT, no hang-watch dump. Side finding: the hook's
first "the LVGL task wedges after release" reading was its OWN bug —
a uint32 countdown double-decrement underflowed and the return-home
leg never fired; the heartbeat watcher (armed, 15 s threshold) stayed
silent throughout, i.e. the LVGL task was alive the whole time.

**Follow-up closed (2026-11-12) — the frozen-touch member (task #31).**
User report: "touch is very unresponsive on the amoled display". The
WDT backtrace named the same class one level ABOVE the panel seam:
``wait_for_flushing`` (lv_refr.c) ← ``lvgl_worker`` (esp_lv_adapter.c)
— LVGL's default flush wait is a non-yielding ``while
(disp->flushing);`` spin, and a lost trans-done hand (the sync
screen's own pre-arm switch flush during the boot sync into a marginal
AP) pins whichever task enters it forever: the LVGL task (its indev
poll dies with it: touch goes dead) — and, until the boot sweep
retired, the sweep task at ``lv_refr_now``. No mutex is held, so the task WDT only prints and the
panel watchdog is blind. Fix: ``moon_flush_wait_cb``
(:ref:`display-lost-dma`) gates on ``s_panel_mtx`` with a 2 s cap — a
free mutex means the flush physically landed, LVGL clears
``disp->flushing`` itself on the callback's return, and the healthy
path never calls it. The ~80 s task-WDT storm the previous binary
produced on the failing-boot-sync window is gone: the same boot
condition now completes, and THREE flash-reset cold boots (5-frame
sweep, bench AP marginal — the sync timeout, the trigger window,
fired 3/3) each ran hold ON → OFF, spin started, hook timers alive,
**zero** task WDT, **zero** flush-wait timeouts (the 2 s cap never
needed to fire), zero aborts. Steady-state CPU from the same captures
(``TEST_CPU_STATS_SEC`` 45, one-core basis, IDLE excluded): home
(hook-held ACTIVE spin) 54–64 % busy, moon_spin 51–57 % of that, lvgl
1–5 %; the hub shows a one-time ~44 % load burst, then 4–5 % idle.
Touch had headroom all along — the unresponsiveness was the wedge
state, now bounded.

**Follow-up closed (2026-11-13) — the light-sleep wake crash (task #32).**
User report: "the s3 amoled just crashed coming back from light sleep
via touch." The line after the blackout named it: ``rst:0xc
(RTC_SW_CPU_RST)`` — a panel-watchdog ``esp_restart``, not a panic (and
with the #31 flush-wait bound installed, ``moon_flush_wait_cb`` never
fired in any capture, so the pin was below that seam). It is the lost
completion family's next member (:ref:`display-lost-dma`), sitting on
the tick's DUSK replay: ``lv_refr_now`` returns with the last strip's
DMA in flight, and when exactly that one trans-done is lost the depth-1
flush queue stays occupied invisibly — the NEXT writer discovers it
(the spin-era witness below reads accordingly).
Capture 1, boot 1: dusk frame 8 lost its last strip's completion at
203.4 s; ~700 ms later the touch wake ran the wake branch's own atomic
flush into the occupied slot, the LVGL task pinned ``s_panel_mtx`` at
~2 s per strip for 4.5 s, and the watchdog logged ``PANEL IO STUCK
... by task lvgl`` and restarted — the "crash". The probe build
(attempt-0 anomaly log, give-up log, mutex-holder name in the STUCK
log) reproduced it with ``task lvgl`` on all three lines; the second
wake in the same capture woke clean, matching the sporadic report.
The fix capture then exposed a second flush site of the same shape on
its very first boot: the BOOT SWEEP's last frame — the tick's atomic
flush — lost its last strip's hand 120 ms before the spin's first
window write was rejected at attempt 0, and after the scan-hold
release repaint lost another one the watchdog named ``task moon_spin``
at 76.9 s. So the catch cannot live on the dusk path alone. Fix:
``moon_panel_drain_verify(site)`` — a 1-px black write at the AMOLED's
bottom edge (outside the moon window — the front-buffer mirror is
untouched — and outside every widget; under a panel hold it is trivially
accepted) run after EVERY sanctioned atomic whole-panel flush: dusk
replay, sweep frame, orbit batch, spin park, wake-static display,
re-bake display, and the screen-switch flush with its fade-in leg
(the spin park site retired with the spin subsystem and the sweep
frame with the boot sweep, both later the same
day; the drain-probe site list in :ref:`display-lost-dma` is
current).
The 20 ms lead-in makes the healthy path an attempt-0 accept (a strip
DMAs in ~10 ms); a loss shows up as draw_wait's ~2 s of rejects and
logs the SITE, then restarts there — before the next writer is frozen,
and during the dusk it is invisible; the watchdog restart was coming
either way. S3-only (``#if !defined(CONFIG_BOARD_P4)``), the P4 DSI
path untouched. Side finding: capture 1's first "timers fired while
LVGL was stuck" reading was an artifact of the file spanning the
watchdog restart — boot 2's uptimes overwrote boot 1's window; grep
the ``rst:0x``/``cpu_start`` markers before reading any window.

**Follow-up (same day) — the probe's own hand, closed by a witness.**
The fix capture (4) caught the probe's tail member: a probe's accept
proves the *previous* write but never its **own** completion. The
dusk-replay probe was accepted ~700 ms before the touch wake; the
wake-join burst (deferred re-bake task starting on core 1 plus its
cache-invalidation storm) ate the probe pixel's trans-done and the
wake flush's strip 1 met the occupied queue — same freeze, one write
later. The first answer chained the write twice (round 2's attempt-0
accept proves round 1 by the depth-1 FIFO) and added a probe BEFORE
the wake's pose flush (``wake preflush``). Capture 5 walked straight
into the hole that left: the preflush probe's own pixel became the
culprit — ``panel draw not accepted: task lvgl (y 0..24); last
accepted: lvgl y465..466 @ 1500 ms ago (the culprit)`` — dusk
interrupted at 8/45, three boots, one signature. No chain of probes
can close that tail, because the next writer that would prove a tail
pixel is itself the frozen victim. The airtight proof therefore comes
from the driver: the io's ``on_color_trans_done`` is the very
completion whose loss starts the family. Its single slot is owned by
the ``esp_lvgl_adapter`` bridge, and in the S3's ``NONE`` tear-avoid
mode that handler *is* LVGL's flush-ready signal — so replacing it
would wedge the refr loop; instead ``moon_panel_witness_arm()``
**chains** it: it reads the bridge's ``{fn, ctx}`` off the io's
private struct (layout pinned to the IDF v5.5.1
``esp_lcd_panel_io_spi.c``; a boot-time arm test — one drain-pixel
write, counter must move — turns any drift into a boot log line or a
bench fault, never a silent loss), registers its own counting callback
through the official single-slot API and forwards untouched.
``moon_panel_drain_verify`` then waits for the probe pixel's own
increment after its post-accept snapshot; a 300 ms silence is
ambiguous (lost completion vs. the µs race of the done landing between
submit and snapshot) and the tie breaks FIFO-wise — a second probe can
only be ACCEPTED if the first completed — while a frozen counter on
the second probe is a genuine loss.

**Follow-up (captures 6–9) — the witness found its own bug, and the
family found its root.** The first witnessed builds turned the witness
into a deterministic *false* positive: four boots restarted at the
first witnessed site (``probe counter frozen at 99/99/119/100`` at
``sweep frame``) — the counter was alive through every boot flush and
dead by the first drain. Instrumenting before trusting the restart
path exposed the whole mechanism, in three steps:

*Capture 7 — probe shape.* The 2-byte probe pixel rode a ``static
const`` FLASH (DROM) buffer — the only color write on the bus that
``spi_master`` heap-bounces per write, and the only write whose io
color callback never fired at ANY site in ANY boot (strips 19/19 per
frame, probe 0/2). Every earlier capture's "probe accepted" was real
while its own completion silently never existed. The probe became a
DMA-capable DRAM full-width row (932 B, single chunk — the exact strip
shape), and the boot-time arm test was rewritten to demand the row's
**own** increment (the old "any ambient increment counts" test had
been riding ambient LVGL flushes and proved nothing).

*Capture 8 — the hold hole.* A live panel hold made ``moon_panel_
draw_wait`` return OK without ever touching the bus, so a drain probe
under a hold was "accepted" while nothing was queued — four boots,
``accepted under live hold (phantom accept)`` then the frozen counter,
at exactly the hold-managed sites (switch fade-in, screen switch). The
drain now no-ops under a live hold (the flush it follows was itself
dropped — there is no hand to consume; the hold's heal repaint owns
recovery) and submits STRICT otherwise, so a hold starting mid-drain
can never phantom-accept.

*Capture 9 — task-dependence → ISR truth.* The remaining freezes were
TASK-dependent: drains on the UI-tick task (the sweep frames) always
passed, while the spin-task park and the LVGL-task switch drains froze
for the full poll window with the ambient counter advancing between
sites. An RTC-FAST mirror (bus-side, off both cores' DCache) plus a
throttled ISR-side log settled it: at a frozen site the mirror sat
dead too — poller cache-blindness ruled out, the callback side was
genuinely absent. Holding ``s_panel_mtx`` across the whole
submit+witness window (no drain call site holds it — audited; healthy
hold ≈ 40 ms, full-anomaly ≈ 620 ms vs the watchdog's 3 s + 3 s)
still reproduced it at the serialized sites — capture 10 restarted
``sweep frame`` and ``switch fade-in``, one loss per boot under the
test hammer.

That localized the family INSIDE the driver, below anything the
public API can reach: the GPSPI ISR has no ``host->polling`` guard
(grep: three uses of the flag, none in ``spi_intr``), so a completion
interrupt still enabled from the strip stream can enter while a
co5300 per-draw polling transaction (CASET/RASET/RAMWR — the io driver
sends commands with ``spi_device_polling_transmit``) is in flight, and
the ISR delivers that polling transaction's pointer into the display
device's ret_queue — one stale entry, which the task-side
``spi_device_polling_end`` then also posts. From there the io
driver's own ``num_trans_inflight`` bookkeeping runs one ahead:
``get_trans_result`` pops the stale entry as a "recycle", and the
unconditional ``memset(trans_pool[0])`` (``esp_lcd_panel_io_spi.c:224``)
clears the still-in-flight transaction's ``en_trans_done_cb`` — the
color callback is skipped, the accept stays genuine. Every member of
the family — the wake-flush freeze, the sweep last frame, the
hold-release repaint stall — is this flag eaten.

The final design therefore treats the two detectable shapes
differently, and the witness is what makes them distinguishable:
**rejected probe** (depth-1 queue never frees, ~2 s of attempt
rejects) = a wedged queue = restart at the site, guarded by a
per-boot budget in ``RTC_NOINIT`` (magic-checked, 3 restarts — a
regression that boot-loops degrades to a working clock instead);
**accepted probe with frozen counter** (queue-exclusive ⇒ the ISR
delivered, the flag was eaten) = heal IN PLACE: a no-op
``esp_lcd_panel_io_tx_param(io, -1, NULL, 0)`` runs the driver's own
drain loop over all inflight entries and re-arms the flush queue, with
the site and slot-ownership evidence logged (``slot MINE`` still
guards a stolen callback slot). If a completion were ever truly
undelivered, that call parks in ``get_trans_result`` under the panel
mutex and the watchdog restarts in 3 s + 3 s — the honest fallback.
Healthy cost stays one row write and ~10 ms per site.

**Verification (capture 11, heal build, 300 s under the full test
hammer).** Boot 1 rode the alarm ring, the 10 s auto-wake hammer and
the settings-return hook with **six** swallowed-completion catches —
``drain verify after park/switch fade-in/screen switch/wake preflush:
completion swallowed (counter frozen at N; slot MINE ...); healing the
flush-queue bookkeeping`` — and every one **continued without a
restart** (the spin parked at the same millisecond, the alarm ring
completed, the counter advanced across heals: 651→673→675→677→700,
i.e. the re-armed flush queue kept flowing). The predicted honest
fallback fired exactly once: the wake-preflush heal parked on a truly
undelivered completion and the watchdog restarted 6.5 s later naming
the parked holder (``PANEL IO STUCK: panel mutex held 3000 ms ... by
task lvgl``). Boot 2 stayed entirely clean to the end of the capture.
Zero drain restarts; the ``RTC_NOINIT`` budget went unused. (The
pre-existing ~30 s ``IDLE1``/``moon_sweep`` task-WDT bite storm during
the boot sweep predates this work and is unaffected — it never
aborts.)

**Follow-up (2026-11-15) — the ladder's healthy path never existed,
and the bake-reveal trace proved it.** The reveal pacing investigation
(:doc:`../implementation/bake-reveal`) instrumented every displayed
reveal state and found the witness billing a flat **~641 ms** each:
every probe that follows a multi-strip ``lv_refr_now`` freezes its own
completion — 6/6 on the reveal legs, consistent with capture 7's own
boot logs ("strips 19/19 per frame, probe 0/2") and with capture 11's
6 heals / 0 restarts. The accepted-probe branch was therefore the
*only* branch ever taken, and the two-probe ladder just delayed the
inevitable heal by two 300 ms poll windows. The witness now grants one
30 ms grace (a genuinely delivered completion lands in ~1–10 ms) and
heals directly — ~53 ms per site; the second probe (its
depth-1-accept-as-proof role moot, the heal being the driver's own
drain loop) is gone. The two detectable shapes and their treatments
above are unchanged; the 20-row reveal, meanwhile, showed exactly two
visible collapsed bands behind a 641-ms toll per state — the tuning
(40/50 one-strip states) is measured in the bake-reveal page.
