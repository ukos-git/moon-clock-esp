The knob display
================

The **Waveshare ESP32-S3-Knob-Touch-LCD-1.8** is the **third board
of the moon-clock family** (P0 family seam + P1 home parity + P2
dial-driven settings + P3 alarm editing + P4 battery gauge,
2026-11-09): the ``knob/`` sibling project links
the clock's shared
``components/{clock, ui, wifi, moon}`` through ``EXTRA_COMPONENT_DIRS``
and answers the board-specific symbol surface with its own
``board``/``imu`` shim — the way the P4 port answered it with its
BSP (:doc:`the Knob board <../devices/knob>`). Since P1 the app on
glass **is the family app**: the shared home screen (moon image +
split-flap HH:MM + alarm row + seconds arc) compiled from
``components/ui`` unchanged, driven through the **render seam**
below; the legacy own-screen view (one moon disk + caption) still
builds behind ``KNOB_UI_ENABLE=n``. What the AMOLED clock carries and
the knob does not: the 0x51
brightness ramp, the panel watchdog (the SH8601 is a **backlit LCD**
with no burn-in exposure and no TE line), the IMU tilt logic (no
chip — the shim answers fixed-upright), and audio (no codec —
``alarm_audio.h`` is answered by a silent stub, so a rule never
*rings*; it *shows*: the alarm screen). The alarm *scheduler* is the
shared one — it loads ``/spiffs/alarm.txt`` and arms its esp_timer
while awake — and the deep-sleep arm extends it with the internal
RTC **TIMER** wake (see the ladder).

The same physics, verified the same way
---------------------------------------

The physics is the clock's, **compiled once**: the shared
``components/moon`` is the single provider of the five core + twelve
vendored ERFA TUs plus the ESP asset backend ``moon_esp_assets.c`` —
its CMakeLists selects ``generated/knob`` under a
``CONFIG_BOARD_KNOB`` branch (first-match ahead of the P4/s3
branches; the clock projects never define the symbol, so their TUs
compile exactly as before), and ``knob_moon`` is glue only. (Before
the P0 seam the knob compiled those TUs itself through relative
source paths — the identical mechanism ``host_sim`` uses; the seam
retired the duplicate file list.) Because of that,
``host_sim/.venv/bin/python host_sim/check_gate.py --target knob``
describes exactly the physics the panel shows: the knob gate reads
**mean MAE 6.45 / SSIM 0.907** (s3 6.68/0.908, p4 6.73/0.907), and
the stale-binary guard ties the sim to the knob's own baked textures
(albedo 864×432 + DEM 1024×512, :doc:`the moon constants
<../reference/moon-consts>`). The DEM is the knob's only reduced
asset: the 1024×512 relief cache is a 1 MiB resident + 0.5 MiB
transient instead of 4 + 2 MiB, and the gate accepted it (0 frames
over the MAE ≤ 12 bar).

The frame path
--------------

One LVGL ``lv_image`` over a PSRAM RGB565 square, through the
existing **partial** flush (two 360×30 internal-DMA buffers):

- a **producer** task bakes frames into a PSRAM double buffer. It is
  CPU1-pinned and TWDT-unregistered
  (``xTaskCreatePinnedToCoreWithCaps``, the clock's bake-producer
  posture — the re-bake/hourly tasks; both sweeps are gone, the
  clock's retired 2026-11-13, the knob's own 2026-11-14), but
  everything long on it **yields**: the core's classify
  scan on a 20 ms wall-clock budget (``moon_event.c``'s
  ``CONFIG_BOARD_KNOB`` branches — the coarse grid *and*
  ``se_argmax``'s 73-sample fine window), the bake by rows — so ``knob/sdkconfig.defaults`` keeps
  ``CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1`` **on** and sets
  ``CONFIG_ESP_TASK_WDT_PANIC``. The bench lesson of 2026-11-11: with
  the check off, an unyielding scan reset the board as a naked
  TG1WDT banner (five boots, no diagnostic); a count-based yield
  (every 32 evaluations) still lost — one coarse stretch plus one
  unyielded fine window summed past the 5 s deadline. A wall-clock
  budget caps every stretch at ~20 ms + one evaluation;
  ``full_moments`` then logs its cost (``N evals in M ms``, once per
  calendar day through the cache);
- a **swap** task (plain registered) applies each frame as ONE
  atomic ``lv_refr_now`` under the main-injected LVGL mutex, with
  publish/apply ``seq_cst`` fences and an ack-before-reuse cap (a
  producer never rewrites a buffer the swap has not acknowledged,
  5 s timeout);
- the descriptor declares plain ``LV_COLOR_FORMAT_RGB565`` and the
  draw unit byte-swaps into the ``RGB565_SWAPPED`` canvas — a purple
  wash on glass would be that swap misconfigured. The F1 fallback
  (single continuous window write for QSPI seam lines) was specified
  in the plan and **not built**: the partial drain never showed seam
  lines on this panel.

Measured on the S3R8 (bench, 2026-11-08; the sweep-era numbers are
dated history — the 128-px intermediates retired 2026-11-14): relief
build 727 ms, first
360-px bake ~790 ms, ~115–130 ms per 128-px sweep frame, 0 TWDT,
~6.8 MB PSRAM free after assets + buffers. Re-measured 2026-11-11
with the WDT posture above: the classify scan is **463 ephemeris
evaluations in 24.6 s** (~53 ms each at ``-Og`` — the ~8 ms the first
fix assumed was off ~7×), sweep frames bake at 132–141 ms, and a boot
that used to die at t≈11–15 s now runs the full scan and 110 s of
presentation with 0 task-WDT, 0 resets.

The render seam (P1)
--------------------

The shared ``components/ui`` compiles into the knob **unchanged**:
the six core TUs (``ui.c``, ``screens.c``, ``images.c``, ``styles.c``,
``actions.c``, ``vars.c``) plus the generated ``fonts_knob/`` tier
(74/37/25 px = the S3's 96/48/32 baselines scaled by 360/466 — the
six file names keep the baseline sizes exactly like ``fonts_p4/``'s,
the baked sizes live in the files; same ``lv_font_conv`` provenance,
identical symbol names so ``fonts.h`` never changes). The four AMOLED/board-coupled
TUs (``moon_ui.c``, ``moon_tfcache.c``, ``alarm_audio.c``,
``demo.c``) drop out of the knob's SRCS, and the knob answers their
public headers from its own components:

- ``knob_moon_render.c`` implements the **``moon_ui.h`` seam** — the
  thirteen functions the shared screens/actions call. The physics
  honest to this board: fades and dim ramps become **LEDC duty
  ramps** (10 ms steps on GPIO 47 — there is no 0x51 QSPI brightness
  path to wedge, and no 0x51 is ever sent); ``moon_ui_screen_switched``
  is the **one atomic whole-panel ``lv_refr_now``** under the
  injected LVGL mutex (the family's hard invariant — and the knob
  keeps exactly **two ``lv_refr_now`` owners**: this seam and the
  swap task); ``moon_ui_create_image`` attaches through the
  producer/swap core's shared PSRAM descriptor; ``moon_ui_day_tick``
  is a **documented no-op** (the S3's minute-freeze lesson,
  :doc:`the animations page <animations>` — the
  classify/event probe is the producer task's job on CPU1, never the
  LVGL tick's), tilt says settled (no
  spin seam at all since 2026-11-13 — the clock's seam API lost its
  panel-step/keep-mask surface with the spin subsystem, and the dusk
  seam entries with the dusk the same day).
- ``knob_alarm_audio_stub.c`` answers ``alarm_audio.h``: ring/preview
  are silent no-ops and every selectable sound is honestly named
  "Silent" (codec-less board) — the shared alarm paths link and run.
- The ``board``/``imu`` shims (P0) stay, plus a silent **name shim**
  component ``esp_codec_dev``: the ui component's REQUIRES must be
  unconditional because the component manager's early requirements
  pass evaluates component CMakeLists with **neither project
  variables nor ``CONFIG_`` symbols** (``build.cmake``'s
  ``__component_get_requirements`` runs ``cmake -P`` on just the
  properties files) — the same reason the knob branches there key on
  the project variable ``KNOB_BOARD``, never on ``CONFIG_BOARD_KNOB``.

The injected mutex must be **reentrant** (bench 2026-11-10): it is
the app-wide ``s_lvgl_mux`` that ``lvgl_task`` holds across
``lv_timer_handler``, and LVGL event handlers run inside that
context. The home tap's chain (``CLICKED`` → hub ``loadScreen`` →
``moon_ui_screen_switched`` → ``knob_ui_invalidate_all``) re-took it
from the task that already owned it — and a FreeRTOS mutex is **not
recursively takable**, so the LVGL task froze on the first
hub-opening tap (no WDT ever fired: the producer tasks kept the
ack/idle path alive). ``knob_ui.c`` wraps every take/release in a
holder-task + depth counter — a same-task take is a depth bump, an
unlock from a non-owner logs an error and is ignored — and
``main.c``'s ``lvgl_lock``/``lvgl_unlock`` delegate to it. The
clocks cannot hit this: their seam locks ``s_rgb_mtx``/``s_panel_mtx``,
never the LVGL task's own mutex, and boot/wake ``loadScreen`` runs
lock-free on other tasks.

``KNOB_UI_ENABLE`` (the ``knob_ui`` Kconfig umbrella) gates the seam
at **runtime**, not in CMake: the seam objects always link (the early
pass forbids conditional REQUIRES anyway), and ``main.c``'s
``#if`` decides whether ``moon_ui_init``/``ui_init`` run — with the
switch off the legacy own-screen view comes up instead. The family
per-second chain rides the shared ``clock_rtc_interrupt_handler_cb``
on an ``lv_timer`` (1300 ms, the clocks' cadence) with exactly the
clocks' three registrations — clock labels + day probe, alarm
display, alarm trigger — and no idle pair: the knob's ladder is
``knob_power``'s, not the app-layer dimming. The idle pair's *other*
legacy is boot-time, though: on the clocks the shared 10 ms UI-tick
and 1 s seconds-increment ``lv_timer`` timers are created by
``action_wake_screen`` — a handler the knob never runs — so the
shared home's seconds arc sat frozen between the minute-tick writes
(the user's "seconds arc is not moving anymore", 2026-11-10).
``main.c`` now calls ``action_ensure_ui_timers()`` (a knob-guarded
bare create/resume of exactly those two timers, no wake side work)
in the single-threaded creation phase; the log line
``UI + seconds lv_timers up`` is the boot evidence.

The opening act
---------------

Every boot and every wake presents the **static** current phase —
one crisp 360-px bake, clocked by the same rule that keeps the
battery honest. The cold-boot synodic sweep (1344 frames new moon →
now at 128 px plus the crisp final frame, **314 s wall**, the
never-black posture) retired 2026-11-14; the clocks dropped their
own 2026-11-13, so the clock's wake policy is now the whole display
policy. The surviving affordance of that posture lives on the shared
home since task #28 through the render seam: the seam attaches a
gated row to the producer's caption staging
(``knob_ui_bind_caption``) — a montserrat-16, 240 px, DOT-centred
label at the **top band** (+28; the middle belongs to the clock
column, the foot to the battery icon), revealed by the swap task
only while the staged text is an honest failure state (``no time``,
``moon unavailable``) — the shared clock row owns the daily text
there. The press-abort and its press observer retired with the
sweep, so every press is an ordinary home/hub touch again. The
legacy own-screen build (``KNOB_UI_ENABLE=n``) keeps its own ungated
bottom caption and screen handler.
The current-phase frame
carries the sky event through the shared tint LUT (the 2025-03-14
total-lunar-eclipse epoch showed the blood tint on glass), and a 1 Hz
day tick re-bakes exactly once when the UTC date or the event
signature changes.

The press-abort bench acceptance for the seam affordances (2026-11-13,
synthetic tap via the then-live ``KNOB_DIAG_TOUCH_INJECT_S`` — tap
into the running sweep, no hub-load lines behind the consumed
gesture) retired with the sweep on 2026-11-14; the surviving probe
facts are the caption row's ``x=60 y=28 w=240 h=18`` (the top band,
centred) and the ladder dimming at idle 60 s after the presentation
with 0 WDT/resets across 130 s.

Time: SNTP truth or nothing
---------------------------

The board has **no RTC**, so the display arms only on SNTP truth —
on the clock's own path since P0: ``components/clock``'s software
time base (the P4's posture, ``CONFIG_CLOCK_USE_PCF85063A=n``) with
``clock_sntp``'s one-shot lwIP POLL over the shared
``components/wifi`` join, which exists for nothing else. An answer
counts only with a plausible epoch (> 2021-01-01); with no fix and
no snapshot
the glass reads ``no time`` and **no phase is ever baked from a
guess** — the clock's never-a-phantom-alarm discipline applied to the
moon. Every trusted answer is also **snapshotted to NVS** — the
P4's pair, namespace ``clock``/key ``epoch``, saved by
``clock_sntp`` and by an explicit ``clock_nvs_save_epoch`` from the
power supervisor before every deep sleep: a wake whose
join + NTP window runs long still arms the static current phase
immediately from the snapshot + uptime — the log says
``arming static phase on NVS snapshot`` so the glass never sits dark
for half a minute on an answer the board already had — and the late
fix re-bakes through the ``show_moon`` force-rebake path. A pre-seam
wake finding only the legacy ``knob_time``/``epoch`` snapshot adopts
it once as *last known, untrusted* and promotes it into the shared
namespace. The age of a snapshot is unverifiable offline, which is
why it is a *fallback arm* (plausibility floor enforced) and never a
trusted instant. Measured: join → fix in ~4–11 s, agreeing with the
host UTC clock within ~2 s. While awake, the fixes are the boot fix
+ the late fix — the clocks' hourly on-demand refresh
(``clock_sntp_needs_sync``) is app-layer and arrives with the shared
screens; the UI reads the system clock once it is valid, so a
correction applies without re-arming. Bench builds freeze a test
epoch (``KNOB_TIME_USE_TEST_EPOCH``) to keep the acceptance epochs
reproducible.

The legacy own-screen view's caption (``KNOB_UI_ENABLE=n``) is a
**local wall clock** ("Nov 8 17:29") formatted through the POSIX zone
in ``KNOB_TZ`` — the same reading the clocks' home screen gives, so
one bench moment shows one time on all three boards (the photo showed
the clock at 17:29 local beside a caption spelling the same moment
"16:27 UTC") — and the 1 Hz day tick rolls it with the minute
**without a re-bake** (the text previously moved only when a
day/event re-bake happened, so between bakes the glass clock sat on
the bake instant). At the round panel's bottom edge the window is
only ~205 px wide, so the label is a fixed 240 px box with center
align and DOT long mode: an over-long caption becomes an ellipsis,
never a clipped date (the 2026-11-08 bench photo clipped both ends of
"2026-11-08 16:27 UTC" at the old auto-width, bottom-6 px position).
The production (P1) build shows the **shared home screen** instead —
the split-flap HH:MM pair cells, colon, alarm row and seconds arc
from ``screens.c``'s ``create_user_widget_clock``, laid out at 360 px
through ``UI_PX()`` — and the caption path stays alive only in the
legacy build (``KNOB_TZ`` retires when the shared TZ screen lands in
P2).

The power ladder
----------------

``knob/components/knob_power`` runs a 500 ms supervisor task:
active → backlight dim → ``esp_deep_sleep_start``, on the **same
store pair the clocks' sleep screens own** (P2): the dim rung fires
after ``settings_idle_timeout_sec`` (the light-sleep screen's roller,
``settings.txt`` ``idle_timeout_sec``, default 60) and the sleep one
countdown after THAT (``settings_deep_sleep_countdown_sec``, default
60) — the clocks' additive posture, both measured from the last
touch/dial activity. The store is clamped to 60..1800 so it always
answers; ``KNOB_DIM_AFTER_S`` (15) and ``KNOB_SLEEP_TIMEOUT_S`` (60)
are the seed fallbacks behind it. The dim rung is
**two-part** like the clocks' light sleep: the supervisor drops the LEDC
duty to ``KNOB_DIM_DUTY`` **64 = 25 %** (the P1 dim-parity value
matching the clocks' light-sleep panel), and a registered dim observer (``knob_power_set_dim_cb`` —
main.c wires it to the seam's ``knob_render_set_home_dim``) drops
the shared clock widget to ``UI_CLOCK_ALPHA_PCT`` (20 %) opacity
alongside it, restoring both on activity. The observer also **hides the
seconds arc** while dimmed: the arc is a sibling of the clock container
in ``screens.c``, so the container's opacity never cascades to it, and it
ticked at full brightness through the dim phase (user 2026-11-11:
"still shows the seconds tick arc in light sleep"); the clocks simply
pause the seconds timer at their light-sleep entry, the knob has no
light-sleep screen, so the rung hides the arc — hiding erases the
ticking too. Activity is
touch contacts (pushed from the input read) and dial detents on
**non-home screens only** — on home the dial is the brightness
control (dial semantics below), so a detent there deliberately is
*not* activity and never un-dims; only **brightening** from a dark
panel reports wake intent (the supervisor's un-dim then also
restores the dimmed clock widget); **the boot arm and the opening act both hold**
the ladder, so a presentation is never interrupted — and never
pre-empted: the hold starts when ``boot_arm_task`` begins, not when
the producer task exists, which is what keeps a slow join + NTP
window from dimming at 60 s and sleeping at 120 s over a glass that
has not shown a single frame yet (only a board that will never
present — no assets, or no fix and no snapshot — leaves the ladder
free to sleep it). Device-verified (P1-era, ladder still on the
Kconfig pair): the then-314 s boot presentation (the sweep, before
its 2026-11-14 retirement) froze both thresholds, then dim fired
at idle 15.1 s and the sleep at idle 60.1 s, with the USB-JTAG node
vanishing host-side (a real deep sleep, not a hang) — the
store-driven rungs re-measure with the P2 bench pass. Before ``esp_deep_sleep_start`` the supervisor
saves the shared ``clock``/``epoch`` NVS snapshot
(``clock_nvs_save_epoch``), so the wake comes up on the last known
instant — and, under ``KNOB_ALARM_TIMER_WAKE`` (default ``y``, the
user's alarm-across-deep-sleep choice), it **also arms the S3
internal RTC timer** for the next fixed rule:
``knob_power_rule_epochs_around`` mirrors the scheduler's own
``mktime``/``tm_isdst=-1`` math over the loaded rule's hour/minute +
weekday mask, and the sleep carries
``esp_sleep_enable_timer_wakeup(delta)`` next to the EXT0/EXT1 pads.
The guards: a rule inside 60 s **defers the sleep** (the awake
app-level esp_timer fires it — no sleeping through your own alarm);
a gap beyond ``KNOB_ALARM_MAX_SLEEP_GAP_S`` (82800 — the Friday→Monday
weekday hole) sleeps GPIO-only; an implausible epoch (< 2020) arms
**no phantom timer** at all. On the other side, ``main.c``'s wake
arbitration decides what a TIMER wake means — the wake-cause legend
on the device page spells it **4**: within ``KNOB_ALARM_GRACE_S``
(300) of the rule recomputed from the snapshot/SNTP it **is** the
alarm → the silent alarm visual via ``action_alarm_triggered`` under
the LVGL lock; an **early** wake within ``KNOB_ALARM_LATE_MAX_S``
(900) holds dark at 1 Hz (re-reading the clock each second so a
landing SNTP fix re-anchors the wait) and fires at the rule — **never
a ring early**; anything further off, or an implausible epoch, is a
stale fire → normal dial-style wake. RC-clock drift (the accepted
cost, bench-tolerance <= ~0.5 %) is absorbed by the grace window and
re-corrected by SNTP on every wake, so it never compounds.

Wake is **EXT0 on dial-A GPIO 8**, active-LOW against the board's own
10K pull-up — the S3 RTCIO accept the plan asked for came back yes.
The EC1 pulses **one channel per detent**, so a detent that happens
to pulse only B (GPIO 7) does *not* wake; the next detent in either
direction pulses A and does (documented, never a dead device).
``KNOB_TOUCH_WAKE`` (default ``y``) additionally arms **EXT1 ANY_LOW
on the CST816S INT line (GPIO 9)** so a screen tap wakes the panel
like a detent does; one touch status read before sleeping clears a
pending INT pulse (a stale low would re-wake the boot — the
boot-loop guard), and a rejected EXT1 pad degrades to dial-only with
a warning. The back-side power button is a hardware rail switch, not
a firmware input. ``KNOB_SLEEP_ALLOW=n`` is the sanctioned log-only
bench mode: the ladder runs verbatim up to the sleep call, then the
board settles into a **stable dark** state (backlight 0, ladder frozen)
— indistinguishable from a truly slept board except that USB-JTAG stays
up. Real activity (a touch, or brightening the home dial) wakes it; the
dial's wake-intent is the software stand-in for the EXT0 dial wake.
(Earlier builds faked activity after the skipped sleep, so the board
un-dimmed to full every countdown and looked wide awake half the time —
user 2026-11-11: "is it really transitioning to light sleep?".)

.. _dial-semantics-knob:

Dial semantics (P2)
-------------------

The dial is an **input device, not a second UI**: it steps one
per-screen edit cursor and a tap commits it. Touch stays fully
native everywhere — the dial is additive, and because there is no
push-button on the EC1, the tap is the *only* commit gesture.

Two files carry it. ``knob/components/knob_ui/knob_dial.c`` runs the
input side: a 20 ms task consumes the debounced encoder count
(vendor pulse-pair scheme, A-rise +1 / B-rise −1) **one detent at a
time** under the LVGL mutex, and every rotation is ladder activity.
Tap-to-commit rides the home tap → hub gesture's own delivery path: a
background CLICKED lands on the **screen root**, where a commit
listener runs inside the LVGL task's lock. One knob-side detail: the
settings creators deliberately clear the root's CLICKABLE — in LVGL 9
a press with no clickable object under the finger has **no target at
all**, so no CLICKED fires anywhere. (The home does keep its root
clickable — but that alone did not make its tap work: the first
hub-opening tap deadlocked on the seam's re-take of the LVGL mutex,
see the reentrant-lock note under the render seam.)
``knob_dial_start`` therefore re-adds CLICKABLE to every settings
root, knob-side only — harmless, the screens are exactly panel-sized,
so nothing can scroll, and the clocks never see the line. The screen
side lives in ``components/ui/screens.c`` behind
``#ifdef CONFIG_BOARD_KNOB`` (the trio ``screens_dial_init`` /
``screens_dial_step`` / ``screens_dial_commit``) — a symbol only the
knob's board-shim ``choice BOARD`` defines, so in the clocks' builds
the block is not preprocessed at all and their UI is structurally
untouched.

The **commit never duplicates handler logic**: it re-sends the very
event the touch path already handles. Icons, the alarm glyphs, exit
buttons, WiFi rows and — since the 2026-11-10 list rework — the knob's
WiFi roller receive a synthetic ``LV_EVENT_CLICKED`` (the same the
finger produces), rollers and the weekday-arc range handles
receive ``LV_EVENT_RELEASED`` — which *is* their commit (store save /
reschedule / alarm-rule save), identical to lifting a finger off a
drag — and the wheel letter goes through
``settings_wheel_add_slot``, the ring's own commit choke point. A
guard makes a stray background tap inert: nothing commits unless a
cursor moved since the screen was entered.

.. list-table::
   :header-rows: 1
   :widths: 26 42 32

   * - Screen
     - Dial turn
     - Tap (background)
   * - Home (modem-sleep screen)
     - **brightness control** (user 2026-11-10): CW brightens, CCW
       dims, 5 % per detent through the seam's
       ``moon_ui_panel_brightness_set``, floored at the ladder's own
       dim rung (``KNOB_DIM_DUTY`` = 25 %) so the dial can never go
       darker than the ladder's choice; a turn is *not* ladder
       activity (the point: fiddling never un-dims), only brightening
       from a dark panel reports wake intent
     - open the hub (the native home-tap handler)
   * - Other moon screens
     - activity, un-dims the ladder (no cursor)
     - open the hub (the native home-tap handler)
   * - Hub ring
     - step focus over the icons + the central exit (white glyph =
       focused, ring on the exit)
     - open the focused icon / exit to home
   * - Alarm screen
     - step the three glyphs (master toggle, pomodoro, alarm clock)
       with a focus ring
     - toggle / open through the native handlers
   * - Roller screens (pomodoro h/m, alarm time h/m, both sleep
       screens, timezone, alarm sound)
     - step the **focused** roller (wraps, animated); pressing the
       other roller of a twin screen re-focuses it
     - ``RELEASED`` = commit (store save / reschedule)
   * - Weekday arc (alarm-time screen, P3)
     - step the **focused** range handle across the seven day cells;
       touch-pressing the other handle re-focuses it (touch-dragging
       the arc stays fully working alongside)
     - ``RELEASED`` on the focused handle = save (the native handler
       writes the rule + re-arms the scheduler)
   * - WiFi list (an ``lv_roller`` on all three boards — user
       2026-11-10 asked for it on the knob ("the label rows read
       too small at 360 px"), 2026-11-11 chose it family-wide, so
       one Wi-Fi selection UI for the clocks too; the 7 native
       rows + sel bar are gone)
     - roll the list — the window's centre **is** the cursor;
       entries in the tier's 48-baseline SSID font (the
       ``_48_ssid`` file: 37 px knob / 48 px S3 / 74 px P4),
       uppercased with the SSID truncated to 14 chars + ``+``
       (the raw name still reaches the keyboard) and the ``NNDBM``
       suffix (the tails clip at the panel's chord — the SSID
       head always reads); NORMAL mode clamps at the ends like a
       real roller
     - open the keyboard / connect (the roller's own ``CLICKED`` —
       the dial commit re-sends exactly that)
   * - Wheel keyboard
     - step the hot slot; the sector reveal follows (the sticky
       reveal a touch press does)
     - commit the hot letter into the running entry
   * - Boot arm / boot bake
     - activity (the ladder holds; a turn never interrupts the act)
     - an ordinary home/hub touch — the press-abort observer retired
       with the boot sweep 2026-11-14

The wheel keyboard's **touch press partition** at knob scale (bench
2026-11-11): letters render at r≈130 px, and the mode register arc —
created after the character catch, so above it in z-order — answered
presses up to r≈121 empirically and swallowed the natural ring press
before the catch ever saw it (zero ``entered letter`` lines, presses
logged as ``settings_wheel: mode``). The knob's build therefore
lifts the catch above the register arc (``lv_obj_move_to_index``, a
``CONFIG_BOARD_KNOB`` branch in ``screens.c``): presses at
r ∈ [108, 180] commit letters through the catch's native annulus,
presses inside it still reach the register arc's four 60° mode keys,
and COMMIT/DISCARD (created later) stay above both. The clocks never
compile the lift.

Battery gauge (P4)
------------------

The gauge is the knob's only addition to the home screen, and it is a
**knob-side overlay** by design: ``knob_battery_ui.c`` adds an
iPhone-style status-bar battery icon to the shared home root after
everything else — a grey-outlined capsule with a right nub and a
proportional fill, click-transparent so the home tap → hub gesture
keeps owning every tap — **bottom-centre** (``BOTTOM_MID`` with a
``UI_PX(26)`` foot lift at the 360 panel — the retired label's
proven spot, the single calibration constant), refreshed by a
one-minute ``lv_timer``:
white fill, red at or below ``KNOB_BATTERY_LOW_PCT``, and while
``knob_battery_charging()`` a hand-rasterised bolt over a fill in the
**theme's moon color** (``theme_colors[active_theme_index][0]``, the
seconds-arc ink, read live so a theme change propagates on the next
minute tick — the reference iOS green replaced 2026-11-14) — a heuristic,
not a signal: a cell read at or above ``KNOB_BATTERY_CHARGING_MV``
(0 = bolt off) is the docked-USB signature (bench ~4522 mV, far over
the cell ceiling); no reading leaves an empty pale-grey capsule.
(Retired with this 2026-11-13 pass: the bottom-centre ``NN %`` digit
label, digits + % on the SSID font.) The cell moves on the minute
scale, not the second. It *overlaps* the moon
rather than making room for it: the shared home bakes the disk
centred and full-square (r 180 at cy 180 — the clocks'
``MOON_R = MOON_PX/2`` posture, set by the render seam via
``knob_ui_set_disk_geom``), while the legacy own-screen view keeps
the foot-band bake (r 150 at cy 168) whose 12 px below centre kept
its bottom-edge caption row clear of the lunar limb (task #35,
2026-11-13 — the user's verdict: the indicator overlaps, the moon
fills). The reading lives in
``knob/components/knob_battery``: ``adc_oneshot`` on **ADC1** ch
``KNOB_BATTERY_ADC_CHANNEL`` (GPIO 1; ADC2 is unusable with the radio
on), 12 dB attenuation, the eFuse curve-fitting calibration with a
linear raw approximation as fallback, scaled by
``KNOB_BATTERY_DIV_NUM``/``_DEN`` across the 10K/10K divider, then
linear between ``KNOB_BATTERY_MV_EMPTY`` and ``KNOB_BATTERY_MV_FULL``
(clamped). ``knob_battery_init`` + ``knob_battery_ui_attach`` run in
``main.c`` inside the P1 umbrella; with ``CONFIG_KNOB_BATTERY_GAUGE=n``
or a failed ADC init every entry point is inert, the icon never
appears, and the log says ``gauge off``. The first read logs
``raw -> mV sense -> mV cell`` — the bench line the multimeter ritual
will check against.

The field-crash witness (gasp + coredump)
-----------------------------------------

The 2026-11-11 field crash (a TG0WDT panic reset during the boot
sweep, right after a screen tap) was **physically unwitnessable**: the
panic reset drained the USB-CDC TX FIFO before any backtrace flushed,
and three bench sessions — full sweep runs, a synthetic tap at the
crash coordinates and timing, the alarm rule armed, warn-mode task
WDT to catch starvation alive — reproduced nothing. The sweeps played
1344/1344 with zero frame gaps and the warn dumps never fired, so the
touch was a **witness, not a trigger**: a rare self-triggering event,
not a touch-path bug. Rather than keep guessing, the knob now carries
its own black box, armed in every production build:

* **The gasp marker** — 24 bytes of ``RTC_NOINIT_ATTR`` memory
  (``s_gasp`` in ``knob_ui.c``) stamped with single atomic word/byte
  stores on the producer/swap/seam/flush hot paths: a stage byte
  (BAKE / WAIT_ACK / PUBLISH / SWAP_LOCK / SWAP_REFR / SWAP_DONE /
  SWITCH\_\* / FLUSH / FLUSH\_DONE; LAND\_BAKE went with the sweep),
  the pub/applied
  frame counters, and the LEDC duty + fade-pending mirror. RTC slow
  memory survives every reset but power-on, so the boot **after** a
  crash prints one line — ``gasp: crash-reboot reason=… pub=…
  applied=… stage=… duty=… fading=…`` — naming the section the board
  was in. The write is magic-gated (``start_tasks`` arms, the report
  clears) and the report is gated on crash reset reasons (panic,
  int-WDT, task-WDT, WDT, brownout), so power-on and dial/tap
  deep-sleep wakes stay quiet. The prime suspect the stages exist to
  discriminate: a **lost GPSPI completion** parks
  ``esp_lcd_panel_draw_bitmap`` on ``portMAX_DELAY`` forever — the
  stock ``esp_driver_spi`` has no watchdog analogue of the clocks'
  ``panel_wdg`` — and the swap would then die at SWAP\_REFR/FLUSH
  with ``pub != applied``.
* **The coredump ring** — a 64 K ``coredump`` partition at the tail
  of ``knob/partitions.csv`` with ``CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH``
  (ELF format): a panic writes an ELF core there that outlives the
  reset. Read back on the bench:
  ``esptool.py --port <port> read_flash 0x710000 0x10000 core.bin``,
  strip the 24-byte flash header, then
  ``espcoredump.py --chip esp32s3 info_corefile -t elf -c core.elf
  knob/knob.elf`` (this IDF tree registers no ``idf.py
  coredump-read`` action). The partition table must be flashed once
  with the new table — the added partition sits after
  ``moon_assets``, so nvs/SPIFFS/assets stay intact.
* **The witness self-test** — ``KNOB_DIAG_PANIC_AT_FRAME`` aborts
  the producer after N published frames; the bench proof (2026-11-12,
  sweep era — the hook then rode sweep frames, since 2026-11-14 it
  rides the static boot bake, where N = 1 is the meaningful value):
  frame 320 → ``abort() was called at PC 0x4201923c on core 1``,
  the boot after printed ``gasp: crash-reboot reason=4 pub=320
  applied=319 stage=0 duty=255 fading=0`` (exactly the crash-time
  state: 320 published, 319 applied, producer just past publish),
  and the flash dump decoded cleanly — ``Found core dump 24324
  bytes``, checksum correct, crashed task ``knob_moon``, PC in
  ``panic_abort``, the abort PC resolving to the hook in what was
  then ``sweep_body``. The one remaining bench-only hook
  (``KNOB_DIAG_FRAME_TRACE`` boundary logs) stays in the Kconfig with
  production default off; the synthetic-tap hook
  (``KNOB_DIAG_TOUCH_INJECT_S``) retired with the sweep — its
  scenario (a tap into a running sweep) is gone.

With the witness shipped, the next field occurrence is answerable
without repro: read the gasp line from the boot log after the crash
and, if the panic path completed, the coredump for the full backtrace
and task list.

Not shipped (on purpose)
------------------------

- **The battery *calibration*.** The code shipped with P4; the honest
  numbers did not. The divider pair and the 3300..4200 mV LiPo pair
  are the plan's sanctioned **placeholder** until the multimeter
  ritual **with the user present** settles whether the divider's top
  rail is the cell or VBUS — the schematic draws it as 5V and the
  factory gauge reads an impossible 4.6 V, so until the measurement
  agrees within ~50 mV the gauge reads *plausible, not verified*. The
  verdict, either way, belongs on the device page.
- **The deep-sleep alarm round-trip bench pass.** Editing is complete
  since P3: the alarm **shows** since P1 (always silent — no codec to
  ring with), and since P2 + P3 the hour/minute rollers, the
  weekday-arc range handles, the master toggle and the pomodoro entry
  are all dial + tap, saved through the native handlers into
  ``/spiffs/alarm.txt`` with the scheduler re-armed. What remains is
  the *sleep-ON* session: the short-countdown TIMER fire on a
  genuinely deep-slept board (the ladder runs log-only for now per
  the bench rule).
- orbit ambient tasks, sub-hour TZ granularity, charge detection
  (no known VBUS sense). Credentials are entered on the glass since
  P2 (WiFi roller → wheel keyboard, COMMIT = save + connect through
  the native handler writing the gitignored credentials file);
  host-side editing of that file stays available.
