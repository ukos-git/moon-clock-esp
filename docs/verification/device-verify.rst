On-device verification
======================

The host gate proves the *math* renders correctly. It cannot prove
the *device* runs correctly: a frame that is pixel-perfect on x86 is
still a half-screen tear if the panel write interleaves with a
re-bake, and a 1.3 s bake that fits in a 5 s WDT window on the
simulator can still starve a FreeRTOS task on the S3 (the Waveshare
ESP32-S3-Touch-AMOLED-1.75). This page is
the second half of the validation: what "verified" means on the
device, the evidence captured on the serial line, and the three
incidents the device — and not the simulator — caught.

The verification surface
------------------------

The device's observable is the **serial console** (115200, the
``esp-idf`` monitor): every pipeline logs its state transitions,
every pipeline timing is a ``ESP_LOGI`` with a measured
``esp_timer_get_time()`` delta, and the Watchdog Timer's fires are
logged by the ROM reset reason. A verification capture is a serial
log of a *defined flow* (boot → sweep → spin → idle → dusk → deep
sleep → wake), read for three classes of line:

.. list-table::
   :header-rows: 1
   :widths: 22 40 38

   * - Class
     - The lines
     - What a presence means
   * - **Progress**
     - the ``sweep span: N d``, ``sweep f/1344 shown in Mms`` (or,
       warm TF replay, the per-frame ``sweep f/N src=tf … wr=…ms``
       lines plus the ``TF sweep: N frames … (~F fps, target T,
       direct write)`` summary), ``dusk k/N shown in Mms``,
       ``spin started`` (300/600 s/turn, 8 fps) and
       ``panel-io watchdog armed (3000 ms)`` lines
     - the pipelines ran to their endpoints (a sweep that never logs
       its final frame is a sweep that stalled)
   * - **Timing**
     - the per-frame ``shown in Mms`` deltas (or the TF replay's
       ``wr=`` column and measured fps), the bake-vs-display
       split, the final full-res bake's wall time
     - the pipeline is *display-paced, not bake-paced* (:doc:`the animation pipelines <../implementation/animations>`);
       a ``shown`` delta that tracks the bake (~1.3 s) instead of the
       display (304 ms measured) is the backpressure failing; a warm
       replay far below its ``UI_MOON_TFCACHE_FPS`` target with a
       large worst-gap is the card or decode path starving the sweep
   * - **Failure**
     - ``WDT reset``, ``panicked``, ``PANEL IO STUCK``,
       ``panel draw not accepted``, ``spin task did not exit``,
       ``ESP_ERR_NO_MEM`` from the flush
     - any one of these is a verification failure; the count over the
       capture window is the acceptance number (0)

The acceptance rule for a pipeline is the triple: **it ran to its
endpoint (progress), it ran at its budgeted rate (timing), and the
capture window had zero failure lines (no WDT, no panic, no bus
stall).** A pipeline that passes progress but not timing is a
performance regression; one that passes timing but shows a WDT is a
lifecycle bug; the measured numbers in this section (:doc:`the animation pipelines <../implementation/animations>` and :doc:`performance <performance>`) are
the *timing* column, quoted from accepted captures.

The boot sweep
--------------

The boot acceptance (the full 1344-frame flow; the bench S3 re-captured
2026-11-08 at the new count — **~36.5 min wall** at the native 400 px
intermediate bake. The bench S3 is the project's own board; the old
"peer's board" label was wrong — see the MAC-based port map in
:doc:`the devices reference <../devices/index>`):

.. code-block:: text
   :caption: Boot sweep acceptance (S3, 2026-11-08 bench capture,
             1344-frame default at the native 400 px bake; the capture
             also shows the full sleep chain after the sweep)

   I (11987) moon_ui: panel-io watchdog armed (3000 ms stuck limit)
   I (15127) moon_ui: synodic boot sweep started: 1344 frames (new moon -> now)
   I (837267) moon_ui: sweep 472/1344 baked in 1210ms @400px (illum 68.2%)
   I (837547) moon_ui: sweep 472/1344 shown in 272ms
   ...
   I (2236617) moon_ui: sweep 1344/1344 baked in 1354ms @400px (illum 1.8%)
   I (2236927) moon_ui: sweep 1344/1344 shown in 281ms
   I (2236927) moon_ui: sweep bake done in 2193384ms
   I (2236927) moon_ui: spin started (active 300 s/turn, settled 600 s/turn, 8 fps)
   I (2236927) clock_idle: Idle monitoring enabled
   I (2297827) clock_idle: Idle timeout reached after 60888 ms
   I (2297827) ui_actions: Switching to light sleep screen
   I (2310517) moon_ui: dusk done; RTC countdown 60 s to deep sleep armed

Three properties are checked: the span is found (the new-moon scan
logged a plausible day count — a degenerate ephemeris logs a full
29.53 d fallback); the per-frame ``shown`` deltas are display-paced,
not the bake (~270–280 ms on the S3's 25-row-strip flush path,
~190 ms on the P4 — never the ~1.2–2 s bake); and the hand-off to the
spin happens (the re-anchor + ``spin started``). The 2026-11-08 S3
capture ran the whole sleep chain on the heels of the sweep: idle
re-arm, ``Idle timeout reached after 60888 ms``, the light-sleep
switch, the 15-frame dusk (shade map 990 ms), the 60 s RTC countdown
and the deep-sleep backlight-off line. The P4 board's
(Waveshare ESP32-P4-WIFI6-Touch-LCD-XC) acceptance is the same flow
on the 650 px disk. The 1344-frame-era capture (2026-11-07, 28 d
old-moon span, 0 WDT/panic): **1344/1344 frames** in **~40.5 min**
wall — bake median **1543 ms** (1138 ms bright → 2083 ms dark, the
2048-albedo PSRAM-load spread), ``shown`` median 187 ms, the spin
hand-off logged at t = 2462 s. In the 618-albedo era the final
full-res bake measured **599 ms @ 618 px / 959 ms @ 650 px** — the
RISC-V vector unit and the X16 PSRAM bandwidth put the P4 ~2–3×
ahead of the S3 per pixel before the 2048 albedo brought the bake
back to the PSRAM-load-latency floor
(:doc:`performance <performance>`).

The spin
--------

The spin's acceptance is a **sustained window** (120 s of continuous
settled spin on the S3), not a boot flow: the spin's failure modes
are the panel-io ones (the lost-DMA-completion stall, the seam lines),
which take a while to show. The seam fix (the single 400×400
window write, ``1582ddb``) was verified with exactly this window:

.. code-block:: text
   :caption: Sustained spin acceptance (S3, 2026-11-01)

   [120 s of settled spin, no user interaction]
   (no PANEL IO STUCK; no WDT reset; no reboots)

The *seam lines* themselves were verified with a diagnostic build
(``TEST_SPIN_BANDS=1``): the spin's 25-row strips become 16 distinct
gray bands (darkest to brightest), so a glitch at a strip seam (a
stray line, a shifted boundary, a row that does not land) shows on a
clean pattern with no moon texture to confound it. The bands build
renders one gray per strip through the *same write path* as the
moon; the diagnostic confirmed the lines at the 25-px band edges,
and the single-window write removed them (the user's visual
confirmation followed the serial acceptance — the serial log proves
the *write path*, the eye proves the *picture*).

The P4's spin is verified the same way, with the async-DSI reject
count added: the ``moon_panel_draw_wait`` retries log a warning only
when a strip is *not accepted after ~100 ms* (a drop), and the
acceptance is **0 such warnings over the window**, with the
throughput-floor rejects (``ESP_ERR_INVALID_STATE`` on back-to-back
calls, retried at 1 ms) counted separately — measured **~3,900
rejects, 0 dropped strips** over a long session, spin-only (0
during the sweep and dusk).

The dusk and the sleep machine
------------------------------

The dusk's acceptance is the *endpoint* and the *countdown arm*: the
dusk runs ``|phi*|`` frames at ~0.2 s/frame (14–95 s), and the
acceptance line is the countdown arming **on the last frame's
display** — not at the entry:

.. code-block:: text
   :caption: Dusk + deep-sleep acceptance (S3, representative)

   I (xxx) moon_ui: dusk 95/95 shown in 156ms
   I (xxx) moon_ui: dusk done; RTC countdown 30 s to deep sleep armed
   I (xxx) actions: Deep sleep: panel backlight off
   I (xxx) actions: UI tick timer paused for deep sleep

The *wake* acceptance checks the wake flow's fix (the 1.7 s freeze,
:doc:`the clock and sleep machine <../implementation/clock-sleep>`): the wake shows the current phase **static** (no sweep),
the backlight ramps back through the serialized 0x51 path, and the
on-demand sync (if owed) runs on the background task — the
acceptance is that the wake screen is *repainted* during the
reconnect (the freeze's signature was the wake screen frozen for the
reconnect's duration; the fix's signature is the seconds arc
advancing while the WiFi sync runs).

The three incidents the device caught
-------------------------------------

The simulator cannot see these three; the serial line and the panel
did.

**1. The NO_MEM mid-flush (:doc:`the display pipeline <../implementation/display>`).** The BSP's original
``buffer_height = 50`` NO_MEM'd partway through a whole-panel flush
(the 46.6 KB strip did not fit the S3's 32 KiB SPI DMA cap + the
~31 KiB largest internal block). The device's signature was the ugliest
possible: the screen showed a *half-updated* image (the strips that
flushed before the failure), LVGL was left with a failed refresh, and
the log said ``ESP_ERR_NO_MEM`` with no context about *which*
transfer. The simulator never hit it — the host's ``malloc`` has no
32 KiB cap and no staging copy. The fix (25 rows, queue depth 1) was
verified by the flush's success over the full boot flow.

**2. The lost-DMA-completion freeze (:doc:`the display pipeline <../implementation/display>`).** A whole-panel LVGL
flush racing the spin's strip writes lost a DMA completion, and the
GPSPI bus lock's REQ/PEND bit stuck: every later panel-io caller
blocked forever in ``dev_wait`` with the panel mutex held. The
device's signature was a *silent, permanent, no-WDT* freeze — the
screen just stopped, no panic, no reboot. The coredump was the
evidence: ``lvgl`` and ``moon_spin`` both parked on the *same*
bus-lock semaphore, **no holder** (the holder's task had been parked
inside the acquire, so the lock's own owner field was empty), and no
WDT fire (parked tasks keep the idles alive). That coredump shape —
two tasks on one bus lock, no holder, no WDT — is the fingerprint of
this failure class, and it is what the watchdog's restart is *for*.
The fix's verification was the three layers: the serialized 0x51
path, the fade hold (``s_fade_busy``), and the watchdog's own
acceptance line (``panel-io watchdog armed``) over a sustained spin
+ fade-storm capture with zero ``PANEL IO STUCK`` lines.

**3. The P4's DPI-FIFO underrun (:doc:`the P4 port <../implementation/p4-port>`).** A 206-row (255 KB)
spin strip on the P4 starved the DPI controller's FIFO ("can't fetch
data from external memory fast enough, underrun") — the X16+XIP
PSRAM sharing the instruction fetch with the strip read. The
device's signature was one underrun per ~28 s of settled spin, a
*visible* tear on the panel (the underrun drops rows from the scanout)
with no log line at all (the DPI controller's underrun is not a
logged error by default). The fix (103 rows → 65 rows for the 650 px
disk) was verified by the underrun disappearing over the sustained
window — and it is a reminder that the panel's *scanout* (not the
CPU's write) is a verification surface: the write can be perfect and
the display still tear if the scanout cannot keep up.

The coredump tooling (a note)
-----------------------------

The freeze's coredump needed two non-obvious facts to read, recorded
here because the next bus-stall investigation will need them: the
coredump's custom 32-byte phdr and 588-byte CORE notes (the
``esp-core-dump`` tooling's expectations), and that the **TCB in the
notes is the thread id** (the task's symbolization requires the
ROM-ELF symbols, not the app ELF's). The "two tasks on one
semaphore, no holder" read is what turned the freeze from a mystery
into a root cause in a day instead of a week.

The standard of verification
----------------------------

The three incidents share a lesson, and it is the standard of this
page: **the device's verification surface is wider than
the CPU's.** The CPU has its logs and its WDT; the panel has its
scanout (underruns the CPU never sees); the bus has its lock (a stuck
state the CPU's caller sees only as an eternal block); the *user's
eye* has the picture (the seam lines were serial-log-invisible — the
write path was correct, the panel's window re-latch was not). A
pipeline is accepted when *all four* surfaces agree: the log says it
ran, the timing says it ran at budget, the panel says it displayed
(no underrun, no seam), and the eye says it is the right picture. The
numbers in :doc:`the animation pipelines <../implementation/animations>` and
:doc:`performance <performance>` carry all four; a number without the
eye's sign-off is a hypothesis, not a verification.

The remaining page of the section is the *performance* of all of it —
the extensive comparison the user asked for (:doc:`performance <performance>`),
with the binary-size chronology as its budget ledger
(:doc:`the binary-size chronology <binary>`).
