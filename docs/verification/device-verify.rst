On-device verification
======================

.. brief:
   Reader: a developer who changed firmware and has to show it runs correctly on the boards, not only in the simulator.
   They already know: how to build and flash a board, ESP-IDF logging, what a watchdog and a coredump are.
   After this page they can: capture a verification log, decide pass or fail with the acceptance triple, and recognise the failure classes the device has shown.
   Page type: reference, with how-to procedures and an incident record.

The host gate proves the math renders correctly; this page proves the device runs correctly.
A frame that is pixel-perfect on x86 can still tear in half on the panel if the panel write interleaves with a re-bake.
A 1.3 s bake that fits a 5 s watchdog (WDT) window in the simulator can still starve a FreeRTOS task on the S3 (the Waveshare ESP32-S3-Touch-AMOLED-1.75).
A build passes on the device when a capture of a defined flow shows three things: every pipeline reached its endpoint, ran at its budgeted rate, and logged zero failure lines.
The page also records the incidents that only the device (not the simulator) caught.

.. contents:: On this page
   :local:
   :depth: 1

What a capture reads
--------------------

The device's observable is the serial console (115200 baud, the ``esp-idf`` monitor).
Every pipeline logs its state transitions.
Every pipeline timing is an ``ESP_LOGI`` line with a measured ``esp_timer_get_time()`` delta.
The ROM reset reason logs every watchdog fire.

A verification capture is a serial log of a defined flow: boot, idle, light sleep, deep sleep, wake.
You read it for three classes of line:

.. list-table::
   :header-rows: 1
   :widths: 22 40 38

   * - Class
     - The lines
     - What a presence means
   * - **Progress**
     - the ``bake | phase N% illum, subsolar (..) | geo Xms bake
       Yms`` + ``bake | rgb565 Xms (total Yms)`` pair (one per boot and
       per day-roll),
       ``Switching to light sleep screen``,
       ``panel dim ramp: brightness 100 -> 25%`` and
       ``RTC countdown %u s to deep sleep armed``, plus the
       ``panel-io watchdog armed (3000 ms)`` lines
     - the pipelines ran to their endpoints (a bake that never logs
       its ``rgb565`` line is a bake that stalled)
   * - **Timing**
     - the bake's wall time and the atomic ``shown`` delta
     - the bake-vs-display split holds (*display-paced, not bake-paced*,
       :doc:`the animation pipelines <../implementation/animations>`):
       a ``shown`` delta that tracks the bake (~1.3 s) instead of the
       display (304 ms measured) is the backpressure failing
   * - **Failure**
     - ``WDT reset``, ``panicked``, ``PANEL IO STUCK``,
       ``panel draw not accepted``,
       ``ESP_ERR_NO_MEM`` from the flush
     - any one of these is a verification failure; the count over the
       capture window is the acceptance number (0)

The acceptance rule for a pipeline is a triple:

1. it ran to its endpoint (progress);
2. it ran at its budgeted rate (timing);
3. the capture window has zero failure lines (no WDT, no panic, no bus stall).

A pipeline that passes progress but not timing is a performance regression.
One that passes timing but shows a WDT is a lifecycle bug.
The measured numbers on :doc:`the animation pipelines <../implementation/animations>` and :doc:`performance <performance>` are the timing column, quoted from accepted captures.

Capture and check a flow
------------------------

Use this after any firmware change that touches a pipeline, the panel path or the sleep machine.
Before you start, identify the board's port (see the board-identification steps in CLAUDE.md).

1. Flash the build and open the monitor with a log file:

   .. code-block:: bash

      idf.py -p <port> flash
      idf.py -p <port> monitor | tee capture.log

2. Let the board boot, and check for the bake pair (``bake | phase …`` then ``bake | rgb565 …``) and the ``panel-io watchdog armed`` line.
3. Leave the board untouched.
   After the idle timeout (60 s by default, ``CONFIG_CLOCK_IDLE_TIMEOUT_SEC``) it logs ``Idle timeout reached`` and switches to the light-sleep screen.
   Check for the light-sleep lines in `Light sleep and deep sleep`_.
4. Wait out the deep-sleep countdown (60 s by default) and check for the deep-sleep lines.
5. Wake the board (touch, tilt or alarm; see :doc:`the clock and sleep machine <../implementation/clock-sleep>`).
   Watch the panel: the seconds arc must keep moving while the background sync runs (see `Wake`_).
6. Count the failure lines over the whole capture:

   .. code-block:: bash

      grep -cE "WDT|panicked|PANEL IO STUCK|panel draw not accepted|ESP_ERR_NO_MEM" capture.log

   The count must be 0.
7. Before you read any time window in the log, find the boot markers:

   .. code-block:: bash

      grep -nE "rst:0x|cpu_start" capture.log

   A file that spans a reboot holds two boots, and the second boot's uptimes overlap the first one's.

If a capture fails, compare its signature with `The incidents the device caught`_.

.. _capture-grammar-verify:

The capture, in pictures
------------------------

This page sets the standard; the three figures below draw the machine that reads it.
Every quoted line was checked against its emitting ``ESP_LOG`` call before it was drawn, and the captions give the source lines.

The first figure is the capture: the defined flow the acceptance is read on.
It has the boot bake pair, the idle pair (at the parsed 60 s ``CONFIG_CLOCK_IDLE_TIMEOUT_SEC``), the light-sleep quartet and the deep-sleep trio.
The light-sleep quartet is the switch, the seconds pause, the ``panel dim ramp`` line and the countdown arm; the light-sleep entry arms the RTC one-shot itself.
The wake's acceptance is not a log line but a signature: the seconds arc advances while the background ``wifi_sync`` reconnects.

The second figure is the grammar.
It maps the three line classes onto the acceptance triple (endpoint, rate and zero failure lines), with the two one-sided verdicts.

The third figure is the four verification surfaces: the log, the WDT and bus lock, the panel's scanout, and the eye.
Each incident hangs off the only surface that could see it.
The dashed blind edges are the figure's point.
Three of the four incidents (the silent lost DMA completion, the DPI-FIFO underrun, the seam lines) were invisible to the log.
That is why the coredump, the scanout and the eye are verification surfaces in their own right.

.. graphviz:: ../_graphs/device-verify.dot
   :alt: The device verification machine: the capture flow, the line grammar, the acceptance triple and the four surfaces.
   :caption: The page's standard in one box. A pipeline is accepted when all four surfaces agree; a number without the eye's sign-off is a hypothesis, not a verification. The host half proves the math renders (:doc:`the image gate <image-gate>`); this machine is the device half.
   :align: center

.. graphviz:: ../_graphs/device-verify-capture.dot
   :alt: The boot-to-wake capture flow, with its three line classes feeding the acceptance triple.
   :caption: Every line quoted against its ``ESP_LOG`` site: boot logs ``bake | phase %.1f%% illum, subsolar (…) | geo Xms bake Yms`` plus ``bake | rgb565 Xms (total Yms)`` (``moon_ui.c:1608``/``:1612``, one pair per boot and per day-roll; a bake that never logs its rgb565 line stalled) behind ``panel-io watchdog armed (3000 ms stuck limit)`` (``:2741``); idle logs ``Idle monitoring enabled`` (``clock_idle.c:138``; re-arm variant ``actions.c:1473``) then ``Idle timeout reached after X ms`` (``:121``) at ``CONFIG_CLOCK_IDLE_TIMEOUT_SEC`` = 60 s (``clock/Kconfig:68``); light sleep logs the switch (``actions.c:1576``), the seconds pause (``:1597``), ``panel dim ramp: brightness 100 -> 25% (240 ms)`` (``moon_ui.c:3257``) and ``RTC countdown %u s to deep sleep armed`` (``actions.c:1613``; the entry arms the RTC one-shot itself, ``actions.c:1576-1613``); deep sleep the trio at ``actions.c:1349``/``:1358``/``:1364``. Progress is witnessed by the boot pair, the dim+arm pair and the wake arc signature; timing reads the boot bake against the shown delta (~304 ms display-paced, not the ~1.3 s bake; backpressure failing would show bake-paced); failure means any WDT/panic line, ``PANEL IO STUCK`` (``moon_ui.c:2622``), ``panel draw not accepted`` (``:702``) or ``ESP_ERR_NO_MEM``, and the count over the window must be 0.
   :align: center

.. graphviz:: ../_graphs/device-verify-surfaces.dot
   :alt: The four verification surfaces (log, WDT and bus lock, scanout, eye) with the four incidents and the blind edges.
   :caption: Three of the four incidents were invisible to the log. The lost DMA completion was silent to both log and WDT (parked tasks keep the idles alive) and was rooted only by the coredump shape ``two tasks on one bus lock, no holder``. The P4 DPI-FIFO underrun logged nothing (the DPI controller does not log underruns by default) and showed as one tear per ~28 s of load, fixed by 103 → 65-row strips. The seam lines were invisible because the write path was correct and the per-strip QSPI re-latch was not; the eye caught it (:commit:`4d361afe`, one continuous window write). The log's one clear catch: ``ESP_ERR_NO_MEM`` mid-flush with a half-updated screen, fixed with 25-row strips and queue depth 1 (the 32 KiB SPI DMA cap; the host malloc has neither cap nor staging). The silent case's witness: the coredump's custom 32 B phdr + 588 B CORE notes; the TCB in the notes is the thread id; symbolize with the ROM-ELF, not the app ELF.
   :align: center

The flows the capture reads have their own diagrams:

- the fade machine (``docs/_graphs/screen-switch.dot``);
- the dim-and-arm machine (``sleep-entry.dot``);
- the hourly refresh's producers and the compute watermark (:doc:`the compute progress <../implementation/compute-progress>`; the hour cache's producer flow is ``tf-hour-cache-dot``);
- the wake doors (``wake-doors.dot``);
- the bus lock (``flush-path.dot``, :doc:`the display pipeline <../implementation/display>`).

The timing-class numbers live in :doc:`the ladders <performance>`.
:ref:`The freshness ladder <freshness-ladder-perf>` draws the other side of the same currency: not how soon the device notices the user, but how old the shown moon may be.

Boot
----

Boot is one static bake.
The acceptance is the ``bake | phase … | geo … bake …`` + ``bake | rgb565 …`` pair on every boot, followed by the sleep chain.
The P4 board (Waveshare ESP32-P4-WIFI6-Touch-LCD-XC) has the same acceptance flow on its 650 px disk.
Captures from the longer boot sweep the firmware once ran are in `Captures from retired flows`_.

Light sleep and deep sleep
--------------------------

Light sleep's acceptance is the static dim entry and the countdown arm at the entry.
The dim ramp lands, the moon tick pauses on the static frame, the seconds timer pauses, and the entry itself arms the RTC (real-time clock) one-shot.
Nothing animates between the switch and the arm.
Deep sleep follows when the countdown fires:

.. code-block:: text
   :caption: Light sleep + deep-sleep acceptance (S3, representative)

   I (xxx) ui_actions: Switching to light sleep screen
   I (xxx) ui_actions: Seconds timer paused for light sleep
   I (xxx) moon_ui: panel dim ramp: brightness 100 -> 25% (240 ms)
   I (xxx) ui_actions: RTC countdown 60 s to deep sleep armed
   I (xxx) actions: Deep sleep: panel backlight off
   I (xxx) actions: UI tick timer paused for deep sleep

Wake
----

The wake acceptance checks the fix for the 1.7 s wake freeze (:doc:`the clock and sleep machine <../implementation/clock-sleep>`).
The wake shows the current phase static.
The backlight ramps back through the serialized 0x51 path.
If a sync is owed, it runs on the background task.
The acceptance is that the wake screen keeps repainting during the reconnect.
The freeze's signature was the wake screen frozen for the whole reconnect; the fix's signature is the seconds arc advancing while the WiFi sync runs.

Panel writes
------------

Every frame reaches the panel as an atomic whole-panel ``lv_refr_now`` flush, and the acceptance covers the write path underneath.
The P4 keeps the async-DSI reject accounting.
The ``moon_panel_draw_wait`` retries log a warning only when a strip is not accepted after the loop of about 100 attempts (about 2 s per strip): that is a dropped strip.
The acceptance is 0 such warnings over the window.
The throughput-floor rejects (``ESP_ERR_INVALID_STATE`` on back-to-back calls, retried at 1 ms) are counted separately.
A long session measured about 3,900 rejects and 0 dropped strips (in the spin era; the same path carries the atomic flushes today).

The incidents the device caught
-------------------------------

The simulator cannot see these four; the serial line, the coredump, the panel and the eye did.

1. The NO_MEM mid-flush (:doc:`the display pipeline <../implementation/display>`)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The BSP's original ``buffer_height = 50`` ran out of memory (NO_MEM) partway through a whole-panel flush.
The 46.6 KB strip did not fit the S3's 32 KiB SPI DMA cap plus the ~31 KiB largest internal block.
On the device the screen showed a half-updated image (the strips that flushed before the failure), and LVGL was left with a failed refresh.
The log said ``ESP_ERR_NO_MEM`` with no hint of which transfer failed.
The simulator never hit it, because the host's ``malloc`` has no 32 KiB cap and no staging copy.
The fix (25 rows, queue depth 1) was verified by the flush succeeding over the full boot flow.

2. The lost-DMA-completion freeze (:doc:`the display pipeline <../implementation/display>`)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A whole-panel LVGL flush racing the spin's strip writes lost a DMA completion, and the GPSPI bus lock's REQ/PEND bit stuck.
Every later panel-io caller blocked forever in ``dev_wait`` with the panel mutex held.
On the device this was a silent, permanent freeze with no WDT fire: the screen stopped, with no panic and no reboot.

The coredump was the evidence.
``lvgl`` and ``moon_spin`` were both parked on the same bus-lock semaphore with no holder.
The holder's task had been parked inside the acquire, so the lock's own owner field was empty.
No WDT fired, because parked tasks keep the idle tasks alive.
That coredump shape (two tasks on one bus lock, no holder, no WDT) is the fingerprint of this failure class, and it is what the panel watchdog's restart is for.

The fix had three layers: the serialized 0x51 path, the fade hold (``s_fade_busy``), and the watchdog itself, whose ``panel-io watchdog armed`` line is part of the acceptance.
It was verified over a sustained spin and fade-storm capture with zero ``PANEL IO STUCK`` lines.
The same failure family kept returning in other forms; :ref:`lost-completion-record` follows it to its root in the SPI driver.

3. The P4's DPI-FIFO underrun (:doc:`the P4 port <../implementation/p4-port>`)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A 206-row (255 KB) spin strip on the P4 starved the DPI controller's FIFO ("can't fetch data from external memory fast enough, underrun").
The X16+XIP PSRAM (external pseudo-static RAM) was shared between instruction fetch and the strip read.
On the device this was one underrun per ~28 s of settled spin: a visible tear on the panel, because the underrun drops rows from the scanout.
It logged nothing, because the DPI controller does not log underruns by default.
The fix (103 rows, then 65 rows for the 650 px disk) was verified by the underrun disappearing over the sustained window.
The panel's scanout, not the CPU's write, is a verification surface: the write can be perfect and the display still tear if the scanout cannot keep up.

4. The seam lines (:doc:`the display pipeline <../implementation/display>`)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Faint lines appeared on the spinning moon at the 25-px band edges.
The serial log could not show them, because the write path was correct; the panel's per-strip QSPI window re-latch was not.
The eye caught them.
A diagnostic build (``TEST_SPIN_BANDS=1``) rendered the spin's 25-row strips as 16 distinct gray bands through the same write path as the moon, and the bands confirmed the lines sat at the band edges.
The fix (:commit:`4d361afe`) writes the moon as one continuous 400×400 window.
It was verified with a 120 s sustained-spin window on the S3.
The spin is gone, but the single-window write path stays underneath the atomic flushes.

Reading a bus-stall coredump
----------------------------

Reading the freeze's coredump needed two facts that are easy to miss; the next bus-stall investigation will need them too.

- The coredump has a custom 32-byte phdr and 588-byte CORE notes, which is what the ``esp-core-dump`` tooling expects.
- The TCB in the notes is the thread id, and symbolizing a task needs the ROM-ELF symbols, not the app ELF's.

With those, the "two tasks on one semaphore, no holder" reading turned the freeze from a mystery into a root cause in a day instead of a week.

The standard: four surfaces agree
---------------------------------

The incidents share one lesson, and it is the standard of this page: the device has more verification surfaces than the CPU.

- The CPU has its logs and its WDT.
- The panel has its scanout, with underruns the CPU never sees.
- The bus has its lock, whose stuck state the CPU's caller sees only as an endless block.
- The eye has the picture: the seam lines were invisible in the serial log because the write path was correct and the panel's window re-latch was not.

A pipeline is accepted when all four agree.
The log says it ran, the timing says it ran at budget, the panel says it displayed (no underrun, no seam), and the eye says it is the right picture.
The numbers in :doc:`the animation pipelines <../implementation/animations>` and :doc:`performance <performance>` carry all four; a number without the eye's sign-off is a hypothesis, not a verification.

.. _lost-completion-record:

The lost-completion record
--------------------------

The S3's QSPI panel bus can lose the completion of a DMA transfer.
When that happens, the next writer to the panel waits forever.
The firmware handles this family today with four guards, all described in :ref:`display-lost-dma`:

- the sync-storm panel-traffic hold, which drops routine panel writes while the radio starts and stops (the settings WiFi scan borrows the same hold);
- a bounded LVGL flush wait (``moon_flush_wait_cb``);
- a drain check (``moon_panel_drain_verify``) after every sanctioned whole-panel flush, with a completion witness;
- a heal in place when a completion is swallowed, and a restart at the site when the queue is wedged, under a per-boot restart budget.

This section is the capture record that led to those guards, in the order it happened.
Several captures predate the removal of the spin and the boot sweep, so the sites they name include those flows.

The alarm-window stall
^^^^^^^^^^^^^^^^^^^^^^

The first formal :doc:`demo take <demo-mode>` (2026-11-08) reproduced the stall in the field on the S3.
``PANEL IO STUCK`` fired in all four cycles the board ran, each time about 29 s after ``phase=alarm``.
The device ticks were 668.7–673.8 s against alarm entries at 642.7–644.8 s; the tour is scripted from boot, so this is a fixed window after boot.
Each time the panel watchdog rebooted the board (``rst:0xc`` RTC_SW_CPU_RST, no panic), and it came back in about 13 s plus the warm re-sweep.
The window was the alarm's wake/dismiss: the 0x51 brightness ramp, the fade-to-black switch and the spin resume overlapped there, the same family as the #96 settings-freeze stall.
That is why the alarm phase runs last in the tour.

The alarm-last re-take (14:16, 28 min, warm caches) confirmed the containment and moved the stall.
The P4 ran three full cycles ending ``sync`` then ``alarm`` with no reboot.
The S3 completed ``dusk`` and ``dark`` every cycle, but the stall fired 29.7 s and 30.6 s after ``dusk`` entry instead of about 29 s after ``alarm`` entry.
Six times out of six it landed about 30 s into a panel-ramp window.
The alarm-ring and dusk windows share one ingredient: the 0x51 hardware brightness ramp running at the same time as panel flushes (dusk dims 100 → 25 %, the alarm wake ramps back up).
That made the #96 family the lead for the follow-up.
The cycle-0 montages cover moons, hub_walk, wifi, wheel, back, dusk, dark and home.

The wake path's WiFi init (:commit:`3b61fa9d`)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The missing ingredient was not the 0x51 ramp but the wake path's on-demand WiFi sync.
A bench hammer (``TEST_AUTO_WAKE_SEC`` auto-wakes 5 s after the DEEP rung, 5-frame sweep, connected AP) stalled on every tilt wake from deep sleep.
The wake came at *t* and ``PANEL IO STUCK`` at wake + 4.0…6.3 s, each time ending in the panel watchdog's controlled restart (``rst:0xc`` RTC_SW_CPU_RST, no panic).

The racing writer was the wake's sync task running ``esp_wifi_init``.
That call loads the full Wi-Fi firmware into RAM and runs RF calibration on the prio-23 driver task (wake to "WiFi initialization finished" ≈ 1.2 s).
Meanwhile the resumed spin's strips and the 10 ms LVGL tick flushes were live on the panel bus, and the storm took the completion of an in-flight QSPI DMA.
All three corollaries held:

- boot runs the identical storm before LVGL exists, and is always clean;
- the alarm wake with the moon paused is clean;
- the guard that makes 0x51 writes ride alone on the brightness API (22c87ad) changed nothing, so the flush traffic, not the 0x51, was the racing writer.

The fix is the sync-storm panel-traffic hold (:ref:`display-lost-dma`).
It arms at sync kickoff, after quiescing the bus.
Until the sync task has taken the radio fully down, every routine panel write is dropped, never blocked.
The hold has a 15 s self-healing cap and a 4 s radio tail, and it repaints the whole screen on release.
The screen-switch frame is exempt at both drop seams (the switch-frame pass, :commit:`4d6914f5`): a screen switch means a person is there, and freezing its frame stranded the panel (see :ref:`display-lost-dma`).

The hammer then caught two more thieves in the same window, plus a leak:

- The still-associated station's DTIM beacon wake (about every 300 ms) stole completions after the release.
  Fix: the sync task deinitialises the radio before it releases the hold, and boot likewise switches to airplane mode before the panel wakes.
- LWIP's DHCP RELEASE was transmitting through the softmac that ``esp_wifi_deinit`` was freeing.
  Fix: the DHCP client stops while the radio is still up, then one 100 ms settle before the free.
- A per-cycle event-handler leak fragmented the DMA-capable heap until the 10th ``esp_wifi_init`` hit NO_MEM inside an ``ESP_ERROR_CHECK`` abort.
  Fix: unregister on every deinit, and log and return instead of aborting.

Final hammer proof (2026-11-12): ten consecutive DEEP-to-wake cycles with zero ``PANEL IO STUCK``, WDT or panic lines.
The largest DMA block settles at a stable 15,360 B floor from wake 4 onward.

The settings WiFi scan (:commit:`9091c023`)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The settings WiFi scan is the same class.
Field report: after leaving the WiFi password screen, the home clock row is blank white with no digits (first reported as a "scrambled watch display after returning from settings").

A headless repro hook, ``TEST_SETTINGS_RETURN_SEC``, writes nothing.
It opens the WiFi settings screen N seconds after boot, dwells 2 s, and exits home.
A dwell shorter than the scan's ~3.9 s puts the home switch's atomic flush inside the running scan.
That is the reported path: the password screen re-enters the list, which restarts the scan, and then the user exits home.

The capture before the fix showed 400 ``co5300`` write failures in about 3 s, plus two silently dropped strips covering exactly the clock row (y 176..214, y 214..252).
The drops are permanent: LVGL answered ready, nothing re-invalidated, and the spin's direct write only covered the moon window.

Fix: the scan session borrows the sync-storm hold through ``moon_ui_panel_traffic_hold_ext`` (45 s cap).
The generic UI tick's ``screens_wifi_scan_hold_poll`` owns the release, so the hold follows the scan, not the screen; leaving mid-scan must not cut the guard.
The release keeps the proven 4 s teardown tail, and its whole-screen repaint heals the storm's drops (:ref:`display-lost-dma`).

After the fix, both the natural exit (dwell longer than the scan) and the hard mid-scan exit run hold on, scan done, release stamped 4000 ms, hold off with a repaint queued.
They log zero ``co5300`` failures, zero dropped strips, no WDT and no hang-watch dump.

Side finding: the hook's first reading ("the LVGL task wedges after release") was the hook's own bug.
A uint32 countdown was decremented twice and underflowed, so the return-home leg never fired.
The heartbeat watcher (armed, 15 s threshold) stayed silent throughout, which shows the LVGL task was alive the whole time.

The frozen touch (:commit:`ba984e9c`)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Field report: "touch is very unresponsive on the amoled display".
The WDT backtrace named the same class one level above the panel seam: ``wait_for_flushing`` (lv_refr.c) called from ``lvgl_worker`` (esp_lv_adapter.c).
LVGL's default flush wait is a non-yielding ``while (disp->flushing);`` spin.
A lost trans-done hand pins whichever task enters it forever.
Here the trigger was the sync screen's own pre-arm switch flush during the boot sync into a marginal AP.
The pinned task was the LVGL task, and its input-device poll died with it, so touch went dead; the boot sweep task at ``lv_refr_now`` was exposed the same way.
No mutex is held, so the task WDT only prints and the panel watchdog cannot see it.

Fix: ``moon_flush_wait_cb`` (:ref:`display-lost-dma`) gates on ``s_panel_mtx`` with a 2 s cap.
A free mutex means the flush has landed, LVGL clears ``disp->flushing`` itself when the callback returns, and the healthy path never calls it.

The previous binary produced an ~80 s task-WDT storm in the failing-boot-sync window; with the fix the same boot condition completes.
Three flash-reset cold boots (5-frame sweep, marginal bench AP, so the sync timed out and the trigger window fired 3/3) each ran hold on then off, spin started, hook timers alive.
They logged zero task WDT, zero flush-wait timeouts (the 2 s cap never needed to fire) and zero aborts.

Steady-state CPU from the same captures (``TEST_CPU_STATS_SEC`` 45, one-core basis, IDLE excluded):

- home (with the hook holding the spin active): 54–64 % busy, of which moon_spin 51–57 % and lvgl 1–5 %;
- the hub: a one-time load burst of about 44 %, then 4–5 % idle.

Touch had headroom all along; the unresponsiveness was the wedged state, which is now bounded.

The light-sleep wake crash and the drain check (:commit:`0d2737f2`)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Field report: "the s3 amoled just crashed coming back from light sleep via touch."
The line after the blackout named it: ``rst:0xc (RTC_SW_CPU_RST)``, a panel-watchdog ``esp_restart``, not a panic.
With the #31 flush-wait bound installed, ``moon_flush_wait_cb`` never fired in any capture, so the pin was below that seam.

This is the next member of the lost-completion family (:ref:`display-lost-dma`), here on the tick's dusk replay.
``lv_refr_now`` returns with the last strip's DMA still in flight.
When exactly that trans-done is lost, the depth-1 flush queue stays occupied without any sign, and the next writer discovers it.

Capture 1, boot 1: dusk frame 8 lost its last strip's completion at 203.4 s.
About 700 ms later the touch wake ran the wake branch's own atomic flush into the occupied slot.
The LVGL task pinned ``s_panel_mtx`` at about 2 s per strip for 4.5 s, and the watchdog logged ``PANEL IO STUCK ... by task lvgl`` and restarted: the "crash".
A probe build (attempt-0 anomaly log, give-up log, mutex-holder name in the STUCK log) reproduced it with ``task lvgl`` on all three lines.
The second wake in the same capture woke clean, which matches the sporadic report.

The fix capture then showed a second flush site of the same shape on its very first boot.
The boot sweep's last frame (the tick's atomic flush) lost its last strip's hand 120 ms before the spin's first window write was rejected at attempt 0.
After the scan-hold release, the repaint lost another one, and the watchdog named ``task moon_spin`` at 76.9 s.
So the check cannot live on the dusk path alone.

Fix: ``moon_panel_drain_verify(site)`` writes a 1-px black probe at the AMOLED's bottom edge after every sanctioned atomic whole-panel flush.
The probe sits outside the moon window (so the front-buffer mirror is untouched) and outside every widget; under a panel hold it is trivially accepted.
The sites at the time were the dusk replay, sweep frame, orbit batch, spin park, wake-static display, re-bake display, and the screen-switch flush with its fade-in leg.
The spin park and sweep frame sites went away with those flows later the same day; the current site list is in :ref:`display-lost-dma`.

A 20 ms lead-in makes the healthy path an attempt-0 accept (a strip DMAs in about 10 ms).
A loss shows up as ``draw_wait``'s ~2 s of rejects, logs the site, and restarts there, before the next writer freezes.
During the dusk the restart is invisible, and the watchdog restart was coming either way.
The check is S3-only (``#if !defined(CONFIG_BOARD_P4)``); the P4 DSI path is untouched.

Side finding: capture 1's first reading ("timers fired while LVGL was stuck") was an artifact of the log file spanning the watchdog restart.
Boot 2's uptimes overwrote boot 1's window.
Grep the ``rst:0x``/``cpu_start`` markers before reading any window (step 7 of `Capture and check a flow`_).

The probe's own completion and the witness
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The same day, fix capture 4 caught the probe's tail member.
A probe's accept proves the previous write, never its own completion.
The dusk-replay probe was accepted about 700 ms before the touch wake.
The wake-join burst (the deferred re-bake task starting on core 1, plus its cache-invalidation storm) ate the probe pixel's trans-done.
The wake flush's strip 1 then met the occupied queue: the same freeze, one write later.

The first answer chained the write twice (round 2's attempt-0 accept proves round 1, by the depth-1 FIFO) and added a probe before the wake's pose flush (``wake preflush``).
Capture 5 walked into the hole that left: the preflush probe's own pixel became the culprit.

.. code-block:: text

   panel draw not accepted: task lvgl (y 0..24); last accepted: lvgl y465..466 @ 1500 ms ago (the culprit)

Dusk was interrupted at 8/45, over three boots, with one signature.
No chain of probes can close that tail, because the next writer that would prove a tail pixel is itself the frozen victim.

The proof therefore comes from the driver.
The io's ``on_color_trans_done`` is the completion whose loss starts the family.
Its single slot belongs to the ``esp_lvgl_adapter`` bridge, and in the S3's ``NONE`` tear-avoid mode that handler is LVGL's flush-ready signal, so replacing it would wedge the refresh loop.
Instead ``moon_panel_witness_arm()`` chains it.
It reads the bridge's ``{fn, ctx}`` from the io's private struct, whose layout is pinned to the IDF v5.5.1 ``esp_lcd_panel_io_spi.c``.
It then registers its own counting callback through the official single-slot API and forwards each call untouched.
A boot-time arm test (one drain-pixel write; the counter must move) turns any layout drift into a boot log line or a bench fault, never a silent loss.

``moon_panel_drain_verify`` then waits for the probe pixel's own counter increment after its post-accept snapshot.
A 300 ms silence is ambiguous: it can be a lost completion, or the microsecond race of the completion landing between submit and snapshot.
The tie breaks by FIFO order: a second probe can only be accepted if the first completed, while a frozen counter on the second probe is a real loss.

Captures 6–10: the witness's own bug, and the root of the family
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The first witnessed builds turned the witness into a deterministic false positive.
Four boots restarted at the first witnessed site (``probe counter frozen at 99/99/119/100`` at ``sweep frame``).
The counter was alive through every boot flush and dead by the first drain.
Instrumenting before trusting the restart path exposed the mechanism in three steps.

*Capture 7: the probe's shape.*
The 2-byte probe pixel came from a ``static const`` buffer in flash (DROM).
It was the only color write on the bus that ``spi_master`` heap-bounces per write, and the only write whose io color callback never fired, at any site in any boot (strips 19/19 per frame, probe 0/2).
Every earlier "probe accepted" was real, but its own completion never existed.
The probe became a DMA-capable DRAM full-width row (932 B, single chunk, the exact strip shape).
The boot-time arm test was rewritten to demand the row's own increment; the old test accepted any ambient increment, rode on ambient LVGL flushes, and proved nothing.

*Capture 8: the hold hole.*
A live panel hold made ``moon_panel_draw_wait`` return OK without touching the bus.
A drain probe under a hold was therefore "accepted" while nothing was queued.
Four boots logged ``accepted under live hold (phantom accept)`` and then the frozen counter, at exactly the hold-managed sites (switch fade-in, screen switch).
The drain now does nothing under a live hold: the flush it follows was itself dropped, so there is no hand to consume, and the hold's heal repaint owns recovery.
Otherwise it submits strictly, so a hold that starts mid-drain can never phantom-accept.

*Capture 9: task dependence, then the ISR's view.*
The remaining freezes depended on the task.
Drains on the UI-tick task (the sweep frames) always passed.
The spin-task park and the LVGL-task switch drains froze for the full poll window, while the ambient counter advanced between sites.
An RTC-FAST mirror (bus-side, outside both cores' DCache) plus a throttled ISR-side log settled it: at a frozen site the mirror was dead too.
That ruled out cache blindness in the poller; the callback really never ran.
Holding ``s_panel_mtx`` across the whole submit-and-witness window (no drain call site holds it, audited; a healthy hold is about 40 ms, a full anomaly about 620 ms, against the watchdog's 3 s + 3 s) still reproduced it at the serialized sites.
Capture 10 restarted at ``sweep frame`` and ``switch fade-in``, one loss per boot under the test hammer.

That placed the family inside the driver, below anything the public API can reach.
The GPSPI ISR has no ``host->polling`` guard (grep: three uses of the flag, none in ``spi_intr``).
A completion interrupt still enabled from the strip stream can therefore enter while a co5300 per-draw polling transaction is in flight.
Those polling transactions are CASET/RASET/RAMWR, because the io driver sends commands with ``spi_device_polling_transmit``.
The ISR delivers the polling transaction's pointer into the display device's ret_queue: one stale entry, which the task-side ``spi_device_polling_end`` then also posts.
From then on, the io driver's own ``num_trans_inflight`` bookkeeping runs one ahead.
``get_trans_result`` pops the stale entry as a "recycle", and the unconditional ``memset(trans_pool[0])`` (``esp_lcd_panel_io_spi.c:224``) clears the still-in-flight transaction's ``en_trans_done_cb``.
The color callback is skipped, and the accept stays genuine.
Every member of the family (the wake-flush freeze, the sweep last frame, the hold-release repaint stall) is this flag being eaten.

Heal in place or restart
^^^^^^^^^^^^^^^^^^^^^^^^

The witness makes the two detectable shapes distinguishable, and the firmware treats them differently:

Rejected probe
   The depth-1 queue never frees (about 2 s of attempt rejects): the queue is wedged.
   The firmware restarts at the site, under a per-boot budget in ``RTC_NOINIT`` (magic-checked, 3 restarts).
   A regression that would boot-loop degrades to a working clock instead.
Accepted probe with a frozen counter
   The queue is exclusive, so the ISR delivered and the flag was eaten.
   The firmware heals in place: a no-op ``esp_lcd_panel_io_tx_param(io, -1, NULL, 0)`` runs the driver's own drain loop over all inflight entries and re-arms the flush queue.
   It logs the site and the slot-ownership evidence (``slot MINE`` still guards against a stolen callback slot).
   If a completion were ever truly undelivered, that call parks in ``get_trans_result`` under the panel mutex, and the watchdog restarts in 3 s + 3 s: the honest fallback.

The healthy cost is one row write and about 10 ms per site.

Verification, capture 11 (heal build, 300 s under the full test hammer).
Boot 1 rode the alarm ring, the 10 s auto-wake hammer and the settings-return hook with six swallowed-completion catches:

.. code-block:: text

   drain verify after park/switch fade-in/screen switch/wake preflush: completion swallowed (counter frozen at N; slot MINE ...); healing the flush-queue bookkeeping

Every one continued without a restart.
The spin parked at the same millisecond, the alarm ring completed, and the counter advanced across heals (651→673→675→677→700), so the re-armed flush queue kept flowing.
The predicted honest fallback fired once: the wake-preflush heal parked on a truly undelivered completion, and the watchdog restarted 6.5 s later naming the parked holder (``PANEL IO STUCK: panel mutex held 3000 ms ... by task lvgl``).
Boot 2 stayed clean to the end of the capture.
There were zero drain restarts, and the ``RTC_NOINIT`` budget went unused.
The ~30 s ``IDLE1``/``moon_sweep`` task-WDT bite storm during the boot sweep predates this work, is unaffected by it, and never aborts.

One grace, then heal (:commit:`9549a2ef`)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ladder's healthy path never existed, and the bake-reveal trace proved it.
The reveal pacing investigation instrumented every displayed reveal state and found the witness billing a flat ~641 ms each.
(Its page was retired with the compute-progress change, :commit:`ee4e3efd`; the numbers live in :doc:`../implementation/compute-progress`.)
Every probe that follows a multi-strip ``lv_refr_now`` freezes its own completion: 6/6 on the reveal legs.
That matches capture 7's boot logs ("strips 19/19 per frame, probe 0/2") and capture 11's 6 heals and 0 restarts.
The accepted-probe branch was the only branch ever taken, and the two-probe ladder only delayed the heal by two 300 ms poll windows.

The witness now grants one 30 ms grace (a delivered completion lands in about 1–10 ms) and heals directly, at about 53 ms per site.
The second probe is gone: its depth-1-accept-as-proof role became moot once the heal was the driver's own drain loop.
The two detectable shapes and their treatments above are unchanged.
The 20-row reveal, meanwhile, showed two visible collapsed bands behind a 641-ms toll per state.
The tuning (40/50 one-strip states) is measured on the compute-progress page (the watermark's strip steps, :doc:`../implementation/compute-progress`).

.. _retired-flow-captures:

Captures from retired flows
---------------------------

The firmware once ran a synodic boot sweep, an animated dusk and a burn-in spin; all three are gone (:doc:`/project/history`).
Their acceptance captures stay here as measured evidence, because other pages quote their numbers.

The boot sweep
^^^^^^^^^^^^^^

The sweep played the cycle from the last new moon before the static boot frame, and needed a longer acceptance.
The bench S3 re-captured the full 1344-frame flow on 2026-11-08 at the native 400 px intermediate bake: about 36.5 min wall.
The bench S3 is the project's own board, not a peer's; see the MAC-based port map in :doc:`the devices reference <../devices/index>`.

.. code-block:: text
   :caption: Boot sweep acceptance (S3, 2026-11-08 bench capture,
             1344-frame default at the native 400 px bake; the capture
             also shows the full sleep chain after the sweep; the
             ``spin started`` line is from the spin era, which ended
             2026-11-13)

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

The sweep acceptance checked three properties:

- The span was found: the new-moon scan logged a plausible day count (a degenerate ephemeris logs a full 29.53 d fallback).
- The per-frame ``shown`` deltas were display-paced, not bake-paced: about 270–280 ms on the S3's 25-row-strip flush path and about 190 ms on the P4, never the ~1.2–2 s bake.
- The capture carried on into the sleep chain.

The sweep and dusk logged per-frame ``sweep f/1344 baked/shown`` and ``dusk k/N shown in Mms`` lines, the per-frame forms of today's progress and timing lines.
The 2026-11-08 S3 capture ran the whole sleep chain after the sweep: idle re-arm, ``Idle timeout reached after 60888 ms``, the light-sleep switch, the 15-frame dusk (shade map 990 ms), the 60 s RTC countdown and the deep-sleep backlight-off line.

The P4's 1344-frame capture (2026-11-07, 28 d old-moon span, 0 WDT or panic) ran 1344/1344 frames in about 40.5 min wall.
The bake median was 1543 ms (1138 ms bright to 2083 ms dark, the 2048-albedo PSRAM-load spread), the ``shown`` median 187 ms, and the spin hand-off logged at t = 2462 s.
In the 618-albedo era the final full-res bake measured 599 ms at 618 px and 959 ms at 650 px.
The RISC-V vector unit and the X16 PSRAM bandwidth put the P4 about 2–3× ahead of the S3 per pixel, until the 2048 albedo brought the bake back to the PSRAM-load-latency floor (:doc:`performance <performance>`).

In the demo takes the sweep ran the 1344-frame spec (693 frames at that day's cycle age) from the last new moon on every device.
With a warm TF cache it replayed (measured 2026-11-08: S3 295 s at 400 px, about 2.3 fps; P4 217 s at 650 px, about 3.2 fps).
Cold, it baked at about 1.5 s per frame: about 17 min on the S3 and about 1120 s (about 19 min) on the P4.
Identical frame pacing and identical illumination curves were the first look-test.
In the first formal take both boards swept 693/693 frames from the warm TF cache (S3 295 s, P4 217 s).
The S3 logged intermittent ``task_wdt`` IDLE0-starvation bursts during the boot sweep, which were non-fatal.

The spin's sustained window
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The spin's acceptance was a sustained window (120 s of continuous settled spin on the S3), not a boot flow.
The spin's failure modes were panel-io ones (the lost-DMA-completion stall, the seam lines), which take a while to show.
The seam fix (the single 400×400 window write, :commit:`4d361afe`) was verified with exactly this window.
The seam lines themselves were confirmed with the ``TEST_SPIN_BANDS=1`` diagnostic build (`4. The seam lines (the display pipeline)`_).
The flow and its diagnostic were removed together with the spin.

Where to go next
----------------

- :doc:`Performance <performance>`: the on-device comparison of all of it, with the timing numbers the accepted captures supply.
- :doc:`The binary-size chronology <binary>`: the flash budget ledger.
- :doc:`The demo tour <demo-mode>`: the scripted tour and the three-board recording harness.
