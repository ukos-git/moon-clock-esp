The knob as a standalone display
================================

The **Waveshare ESP32-S3-Knob-Touch-LCD-1.8** carries a
standalone moon-display firmware as the ``knob/`` sibling project
(:doc:`the Knob board <../devices/knob>`). It is *not* a port of the
clock app: it shares the clock's portable moon core and asset
pipeline verbatim, and nothing else. What the AMOLED clock carries
and the knob deliberately does not: the burn-in spin, the keep-mask,
the 0x51 brightness ramp, the panel watchdog (the SH8601 is a
**backlit LCD** with no burn-in exposure and no TE line), the IMU
tilt logic, the alarm stack and the settings screens. One view: one
moon disk and a caption.

The same physics, verified the same way
---------------------------------------

``knob/components/knob_moon`` compiles the clock's
``components/moon/core`` translation units **verbatim** through
relative source paths — the identical mechanism ``host_sim`` uses
(the file lists are the same five core + twelve vendored ERFA TUs,
plus the ESP asset backend ``moon_esp_assets.c``). Because of that,
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
  (``xTaskCreatePinnedToCoreWithCaps``, the clock's sweep-task
  posture) because the core's classify scan runs ~20 s and the
  new-moon scan ~8 s without yielding — ``knob/sdkconfig.defaults``
  therefore drops ``CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1`` like
  the clock's S3 defaults do;
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

Measured on the S3R8 (bench, 2026-11-08): relief build 727 ms, first
360-px bake ~790 ms, ~115–130 ms per 128-px sweep frame, 0 TWDT,
~6.8 MB PSRAM free after assets + buffers.

The opening act
---------------

Cold boot plays the full synodic sweep — 1344 frames new moon → now
at 128 px (``KNOB_MOON_SWEEP_FRAMES``; the clock's task #17 no-jump
count), then one crisp 360-px final frame: **314 s wall**, logging
``sweeping n/N`` from frame 1 (the never-black posture). Deep-sleep
and dial wakes show the static current phase instead — the clock's
wake policy, and the same rule keeps the battery honest. A screen
press aborts the sweep onto the true phase. The current-phase frame
carries the sky event through the shared tint LUT (the 2025-03-14
total-lunar-eclipse epoch showed the blood tint on glass), and a 1 Hz
day tick re-bakes exactly once when the UTC date or the event
signature changes.

Time: SNTP truth or nothing
---------------------------

The board has **no RTC**, so ``knob/components/knob_time`` arms the
display only on SNTP truth (one-shot lwIP POLL over the board's own
Wi-Fi join, which exists for nothing else). An answer counts only
with a plausible epoch (> 2021-01-01); with no fix and no snapshot
the glass reads ``no time`` and **no phase is ever baked from a
guess** — the clock's never-a-phantom-alarm discipline applied to the
moon. Every plausible answer is also **snapshotted to NVS** (the
clock's ``clock_sntp`` snapshot discipline, plus an explicit save
from the power supervisor before every deep sleep): a wake whose
join + NTP window runs long still arms the static current phase
immediately from the snapshot + uptime — the log says
``arming static phase on NVS snapshot`` so the glass never sits dark
for half a minute on an answer the board already had — and the late
fix re-bakes through the ``show_moon`` force-rebake path. The age of
a snapshot is unverifiable offline, which is why it is a *fallback
arm* (plausibility floor enforced) and never a trusted instant.
Measured: join → fix in ~4–11 s, agreeing with the host UTC clock
within ~2 s; hourly resync rides ``CONFIG_LWIP_SNTP_UPDATE_DELAY``
and applies without re-arming because the UI reads the system clock
once it is valid. Bench builds freeze a test epoch
(``KNOB_TIME_USE_TEST_EPOCH``) to keep the acceptance epochs
reproducible.

The caption is a **local wall clock** ("Nov 8 17:29") formatted
through the POSIX zone in ``KNOB_TZ`` — the same reading the clocks'
home screen gives, so one bench moment shows one time on all three
boards (the photo showed the clock at 17:29 local beside a caption
spelling the same moment "16:27 UTC") — and the 1 Hz day tick rolls
it with the minute **without a re-bake** (the text previously moved
only when a day/event re-bake happened, so between bakes the glass
clock sat on the bake instant). At the round panel's bottom edge the
window is only ~205 px wide, so the label is a fixed 240 px box with
center align and DOT long mode: an over-long caption becomes an
ellipsis, never a clipped date (the 2026-11-08 bench photo clipped
both ends of "2026-11-08 16:27 UTC" at the old auto-width, bottom-6
px position).

The power ladder
----------------

``knob/components/knob_power`` runs a 500 ms supervisor task:
active → backlight dim (``KNOB_DIM_AFTER_S`` 15 → LEDC duty
``KNOB_DIM_DUTY`` 40) → ``esp_deep_sleep_start`` after
``KNOB_SLEEP_TIMEOUT_S`` (60) of touch/dial silence. Activity is
touch contacts (pushed from the input read) and dial detents (the
encoder count polled); **the boot arm and the opening act both hold**
the ladder, so a presentation is never interrupted — and never
pre-empted: the hold starts when ``boot_arm_task`` begins, not when
the producer task exists, which is what keeps a slow join + NTP
window from dimming at 15 s and sleeping at 60 s over a glass that
has not shown a single frame yet (only a board that will never
present — no assets, or no fix and no snapshot — leaves the ladder
free to sleep it). Device-verified: the 314 s sweep froze both
thresholds, then dim fired at idle 15.1 s and the sleep at idle
60.1 s, with the USB-JTAG node vanishing host-side (a real deep
sleep, not a hang). Before ``esp_deep_sleep_start`` the supervisor
saves the ``knob_time`` NVS snapshot, so the wake comes up on the
last known instant.

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
bench mode.

Not shipped (on purpose)
------------------------

- **The battery gauge.** ``adc_oneshot`` on GPIO 1 behind the
  10K/10K divider only enters the firmware after the multimeter
  ritual **with the user present** settles whether the divider's top
  rail is the cell or VBUS — the schematic draws it as 5V and the
  factory gauge reads an impossible 4.6 V, so the honest read is
  *none* until the measurement agrees within ~50 mV. The verdict,
  either way, belongs on the device page.
- **The settings editor** (tz offset, brightness, sleep timeout via
  a ``knob_settings.txt`` + dial stepper) — the dial's next job.
- On-device credential entry (the gitignored credentials file plus
  host-side editing is the input tool), dusk/orbit ambient tasks,
  sub-hour TZ granularity, charge detection (no known VBUS sense).
