System architecture
===================

.. brief:
   Reader: a firmware developer about to change components/ui or components/moon.
   They already know: C, what an RTOS task and a mutex are, the science section's idea of a bake.
   After this page they can: say which task and core runs a piece of work, which buffer it may write under which lock, and which rules a change must not break.
   Page type: explanation.

This page is the map of the firmware.
It names the tasks and the CPU core each runs on, the buffers that hold a moon image, the two locks that guard them, and the rules every change must keep.
The other pages of this section refer back to it, so read it first.

The map in one screen
---------------------

The device has one screen, and a complete moon image takes over a second to compute.
The whole design follows from one demand: the viewer never sees a frame that is half computed or half drawn.

Tasks and cores
   The ESP32 has two CPU cores.
   Core 0 runs the LVGL task (the painter): it draws the screens, reads touch, updates the clock and runs the 50 ms moon tick.
   Core 0 also runs the panel watchdog.
   Core 1 runs every moon bake (about 1.3 s on the S3), each as its own task.
   A bake never shares a core with the painter, so it cannot stall the screen.
   The network sync task is unpinned.
   See `The tasks and their cores`_.

Buffers
   A producer bakes into the bounce buffer ``s_hourly_gray`` without holding any lock.
   It then publishes in one guarded block: a copy into ``s_frame`` (the complete gray frame) and a colour conversion into ``s_rgb`` (RGB565).
   ``s_rgb`` is the only buffer the screen reads.
   The 4 MB relief gradient cache, built once at boot, feeds every bake.
   See `The buffers and who touches them`_.

Locks
   ``s_rgb_mtx`` (the image key) guards ``s_frame`` and ``s_rgb``.
   ``s_panel_mtx`` (the panel key) guards every transaction on the panel bus.
   See `The guards in one picture`_.

These are the rules a change must keep.
`CLAUDE.md` in the repository root is the authoritative list; this is the part that concerns the firmware.

1. Every moon-pipeline frame reaches the panel as one atomic whole-panel ``lv_refr_now`` under ``s_rgb_mtx`` (:cfunc:`moon_ui_screen_switched`).
   The only exception is the hardware brightness ramp (CO5300 command 0x51, or the P4 backlight), which writes no pixels.
   Two flows use it: the fade-to-black screen switch (``UI_SCREEN_SWITCH_FADE``) and the light-sleep dim.
   See `The atomic-frame invariant`_.
2. A frame that is being computed never rides the ramp.
   The old frame stays up for the whole bake (at boot, with nothing on the glass yet, the quick flat phase disc).
   The core-1 producer publishes a rows-completed watermark per strip, and the seconds arc on the home and Wi-Fi screens shows it as a progress bar.
   The finished frame lands in one flush in the bright panel (:doc:`the compute progress <compute-progress>`).
3. ``s_frame`` is never written partly.
   Only the bounce buffer is filled strip by strip, so every resting state of the panel is a complete frame, and an aborted bake leaves nothing to repair.
4. The S3's PSRAM (pseudo-static RAM, the large external RAM chip) cannot feed the SPI DMA engine.
   Flush strips are staged through internal RAM, so the BSP ``buffer_height`` stays at 25 and the flush queue depth at 1 (:doc:`the display pipeline <display>`).
5. AMOLED burn-in is an accepted trade-off.
   The S3 shows the moon statically, and a faint burned-in moon is expected.
   Do not add anti-burn-in machinery (rotation, pixel shift) without a new product decision (:doc:`the animation pipelines <animations>`).
6. A Kconfig option set to ``n`` leaves its macro undefined.
   Runtime values go through ``#ifndef``-seeded fallbacks, never through ``#define`` redefinitions of a Kconfig macro.
7. ``managed_components/`` is vendored code: use its public API only and never edit the files.

Two more properties shape the code.
The physics is portable: the moon core (ephemeris, bake, relief, rotation, the colour table) is a C library with no dependencies, and it compiles unchanged into the device and the host simulator (:doc:`the C core <core-c>`).
Everything specific to a board sits above it: the board shim, the LVGL screens, the panel write path.
And the display is also the sleep surface: light sleep shows the same frame, dimmed by the same ramp, so a sleep transition is a brightness change and not a separate animation.

Vocabulary
----------

These terms appear throughout the section.
The display terms also have entries in the :doc:`glossary <../glossary>`.

- **Task**: one independently running program.
  The real-time OS (FreeRTOS) switches between tasks so fast that they appear to run at once.
- **Core / pinning**: a core is one physical CPU; the ESP32-S3 has two.
  Pinning binds a task to one named core, which is how the design keeps bakes and the painter apart.
- **Mutex (lock) / critical section**: a key with one holder at a time; everyone else waits.
  The critical section is the code that runs while holding the key.
- **Atomic**: the viewer sees all of a change or none of it, with no visible half-state (glossary: *atomic frame*).
- **Flush / strip**: a flush copies a patch of pixels from RAM to the panel.
  Flushes travel as horizontal strips (25 rows on the S3).
  LVGL's default drain repaints a screen strip by strip, top to bottom, over about a second.
  The atomic rule exists to hide that drain.
- **DMA**: direct memory access, a copy engine that moves bytes without the CPU.
  The S3's SPI DMA carries at most 32 KiB per descriptor and reads only internal RAM, so flush strips are staged there.
- **PSRAM**: the large RAM chip beside the ESP32.
  It holds the frame buffers, but the panel DMA cannot read it directly.
- **LVGL**: the graphics library that keeps the tree of screens, labels and arcs and turns it into pixel writes.
- **Bake / producer**: a bake is the computation of one complete moon frame from the ephemeris and the textures (about 1.3 s).
  The tasks that run bakes (orbit, rebake, hourly, the parallel boot bake) are the producers; they publish into the shared buffers.
- **LUT convert**: the last pipeline step.
  A 256-entry look-up table (LUT) turns each 8-bit gray pixel into a 16-bit RGB565 colour in one pass over the frame (a gather).
  With the observer view on, the same gather is turned by the Moon's parallactic angle for the time zone's reference city (``moon_rotate``).
  The bake stays north-up and the tilt is applied only in this step (:doc:`the observer view <observer-view>`).
- **Relief gradient cache**: the 4 MB array of precomputed terrain slopes, built once at boot and read by every bake.
- **BSP / board shim**: the BSP (board support package) is the vendor's low-level driver for the panel chip.
  The shim (``components/board``) is the project's thin layer that makes the S3 and the P4 look like one machine to the code above it.
- **Panel watchdog (``panel_wdg``)**: the guard task.
  If the panel key is not handed back within 3 s (a wedged bus), it restarts the device instead of leaving a frozen screen (glossary: *panel-io watchdog*).
- **0x51 brightness ramp**: the panel command (CO5300 ``0x51``, or the P4 backlight PWM) that dims or brightens the display without writing pixels.
  That is why the fades fit inside the atomic rule.

How the layers depend on each other
-----------------------------------

Each layer may call only the layers below it, and each owns one kind of knowledge.
``moon`` knows moon pixels, ``ui`` knows screens, ``clock`` knows minutes, ``board`` knows the silicon, and the vendored BSPs know the exact panel chips.
The moon core has no dependencies at all, which is what lets the same source compile into the device and the host simulator.
The shim is the one place where the S3 and the P4 differ; above it, every call site says ``board_panel()``.

.. raw:: html

   <pre class="mermaid">
   flowchart TB
     main["main.cpp — app_main: BSP bring-up, wifi boot, clock init, static moon bake, deep-sleep entry"]
     clock["components/clock — minute display, NTP, idle/sleep state, alarm scheduler, timezone, settings"]
     wifi["components/wifi — wifi_manager, wifi scan"]
     ui["components/ui — screens, navigation + wake/sleep flow, the moon pipeline (bake · orbit · panel write)"]
     moon["components/moon — the portable core (Ch. 11): ephemeris, bake, render, Q15.15, assets"]
     board["components/board — the board shim: display / panel / touch / IMU / RTC (S3 + P4, one API)"]
     bsp["vendored BSPs — S3: CO5300 QSPI · P4: JD9365 MIPI-DSI"]
     main --> clock
     main --> wifi
     main --> ui
     ui --> moon
     clock --> board
     wifi --> board
     ui --> board
     board --> bsp
   </pre>

Read the stack top-down: each box calls the boxes below it.
The ``components/moon`` core calls nothing; :doc:`the C core <core-c>` walks it.
The pages that own each layer have the per-file detail:
the clock files are in :doc:`the clock and sleep machine <clock-sleep>`,
the UI files in :doc:`the animation pipelines <animations>` and :doc:`the clock and sleep machine <clock-sleep>`,
the core in :doc:`the C core <core-c>`,
and the shim and BSPs in :doc:`the display pipeline <display>` and :doc:`the P4 port <p4-port>`.

The board shim is where the S3 (the Waveshare ESP32-S3-Touch-AMOLED-1.75) and the P4 (the Waveshare ESP32-P4-WIFI6-Touch-LCD-XC, "4C") stop being two codebases.
Every call site sees ``board_panel()`` and ``board_display()``, and only the shim's two implementations know which silicon is underneath.

The tasks and their cores
-------------------------

The device always runs about five tasks on FreeRTOS.
The table lists who runs, on which core, and what each owns.
The design rests on one constraint: the LVGL painter must answer every 50 ms, so every CPU-bound bake is pinned to core 1 and runs as its own task.
A 1.3 s bake then cannot stall the tick that shows the previous frame.

On the S3 the BSP pins the painter to core 0 itself (it sets ``task_core_id = 0``).
On the P4 the painter keeps the adapter default of no affinity, and the split rests on the producers' pinning alone.
:ref:`The crew sketch <task-roster-arch>` draws both.

.. list-table::
   :header-rows: 1
   :widths: 18 10 40 32

   * - Task
     - Core
     - Pinned?
     - Owns
   * - ``main`` (app_main)
     - 0
     - no
     - the boot sequence (it spawns the boot presentation's two core-1 tasks, the parallel boot bake and the boot replay producer; with the replay off it bakes the static current phase itself),
       deep-sleep entry
   * - ``LVGL`` (adapter)
     - 0 on the S3; either on the P4
     - S3 yes (the BSP sets ``task_core_id = 0``); P4 no (the
       adapter default ``-1``, "no affinity", passes through)
     - the screen tree, the 50 ms ``moon_tick`` lv_timer, the
       minute-driven clock update, the fade and dim ramps
   * - bake tasks (orbit / rebake / hourly / boot bake)
     - 1
     - yes (pinned; internal-RAM stacks, because ``pvPortMalloc`` is
       ``heap_caps_malloc(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)``;
       only the classify worker ``moon_event`` has a PSRAM stack, via
       ``xTaskCreatePinnedToCoreWithCaps``)
     - ``moon_bake`` per frame into the shared PSRAM buffers.
       ``moon_rebake`` re-bakes the static frame off the LVGL task.
       ``moon_hourly`` produces the hourly frame (a bake or a TF hour-cache fetch).
       ``moon_bake`` (the boot presentation's parallel live bake, :doc:`the boot movie <moon-movie>`) bakes the current phase in parallel with the looping movie; it waits for the clock sync first and sets ``s_boot_bake_done`` for the producer.
       ``moon_replay`` (the producer) plays the flash movie in a loop (or the card's stored hours, ascending, as the fallback) and publishes the final now-frame.
       The tick shows each published frame atomically.
   * - ``panel_wdg``
     - 0
     - yes
     - the panel-io watchdog (:doc:`the display pipeline <display>`): a test take of the
       panel mutex bounded at 3 s, ``esp_restart()`` on overrun
   * - ``wifi_sync`` (background)
     - none
     - no
     - ``wifi_manager_init`` and the SNTP sync, off the LVGL task (this keeps the wake from freezing for 1.7 s, :doc:`the clock and sleep machine <clock-sleep>`)

The Core column splits the crew.
The painter and its watchdog sit on core 0 (on the P4 the painter may run on either core), every bake producer sits on core 1, and ``wifi_sync`` is pinned to neither, because its only job is slow network work out of everyone's way.
The Pinned column says yes exactly where timing matters.
``panel_wdg`` owns a watchdog whose only job is to notice when a task never hands back the panel key.

The bake producers (orbit, rebake, hourly) are pinned to core 1 because a bake is CPU-bound.
The LVGL task (with touch, the minute update and the fade ramps) must never share a core with a bake or a gather.
The S3 BSP enforces the painter's half of that split and states the failure mode in its comment: "The adapter default (core -1, no affinity) lets FreeRTOS park it on core 1, where the moon bake producer (pinned core 1, priority ``configMAX_PRIORITIES - 3``) starves it" (``components/esp32_s3_touch_amoled_1_75/esp32_s3_touch_amoled_1_75.c:581-590``).
The P4's ``main.cpp`` passes ``ESP_LV_ADAPTER_DEFAULT_CONFIG()`` through and overrides only ``task_stack_size = 12288``.
Its painter therefore keeps the adapter default ``ESP_LV_ADAPTER_DEFAULT_TASK_CORE_ID`` (``-1``, no affinity), and the producers' pinning carries the split there.

The pipelines also run as tasks, not on the LVGL task, for a second reason.
A 1.3 s bake must not block the 50 ms tick; the tick's only pipeline job is to show what the producers publish.

This roster leaves the priorities out.
The full priority ladder, with the knob rows, the band the ``esp_timer`` daemon shares with the producers and each producer's task-watchdog setting, is in :ref:`the scheduler ladder <scheduler-ladder-perf>`.

.. _task-roster-arch:

The crew in one picture
-----------------------

The crew is drawn in the bring-up style: one overview with every box sealed, then one detail sketch per box.
The detail sketches cover the clocks' resident tasks, the on-demand tasks, the knob's tasks, and the silicon they all query.
The arrow legend is the same on every sketch.
Blue is an ``xTaskCreate*`` spawn, dashed grey is a hand-back (a done flag, a published frame, a watermark), and amber is a periodic query to a chip.

.. graphviz:: ../_graphs/task-roster.dot
   :alt: An overview of the tasks as five sealed boxes (app_main, the painter, the core-1 bakes, the on-demand one-shots, the knob tasks), each joined to a silicon box by cadence arrows.
   :align: center
   :caption: The tasks in sealed boxes: who spawns whom, who wakes whom, and which chip each one queries. The roster table above says who runs where. The sketch holds three claims. (1) The painter's affinity is per board: the S3 BSP pins it to core 0 with its own comment (quoted above), the P4 passes the adapter default through. (2) Every stack is internal RAM by definition, because ``pvPortMalloc`` is ``heap_caps_malloc(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`` (``~/esp/esp-idf/components/freertos/heap_idf.c:43,:47``). The one exception is ``moon_event``, created with ``xTaskCreatePinnedToCoreWithCaps`` on ``MALLOC_CAP_SPIRAM``; ``knob_moon`` asks ``MALLOC_CAP_INTERNAL`` on the same call (:commit:`d7edb36f`: a task that can write flash must not have its stack in PSRAM, which the flash write disables, :doc:`/implementation/knob-display`). (3) The static tasks never exit and every dynamic task deletes itself; the two dashed ghosts are compiled out of the shipped configs. Their priorities are :ref:`the scheduler ladder <scheduler-ladder-perf>`, their resident bytes the :ref:`static footprint <static-footprint-perf>`, and the cadences on the amber arrows :ref:`the silicon ladder <silicon-ladder-perf>`.

.. graphviz:: ../_graphs/task-roster-clocks.dot
   :alt: The clocks' tasks opened: app_main and the LVGL task spawn panel_wdg, moon_event, moon_replay, moon_hourly and moon_rebake; dashed hand-backs return to the painter, and two disabled tasks hang off it in grey.
   :align: center
   :caption: The clocks' tasks (S3 and P4, one source). Boot: ``bsp_display_start`` runs the adapter's LVGL task (``main.cpp:558-572`` P4 branch; the S3 falls through ``bsp_display_start``). ``moon_ui_init`` spawns ``panel_wdg`` (``xTaskCreatePinnedToCore`` "panel_wdg" 4096, prio 3, core 0: "Low priority on core 0: it sleeps 3 s at a time") and the classify worker ``moon_event`` (``xTaskCreatePinnedToCoreWithCaps`` "moon_event" 12288, ``MAX_PRIORITIES-4``, core 1, ``MALLOC_CAP_SPIRAM``: "IDLE1 is not WDT-watched, so its 15-20 s scan can never trip the watchdog"). ``moon_ui_start`` spawns the boot presentation's two core-1 tasks under ``#if CONFIG_UI_MOON_BOOT_REPLAY && !CONFIG_UI_DEMO_MODE`` (=y on both clocks; ``sdkconfig:2359`` / ``sdkconfig.p4:2490``). First ``moon_bake`` (the parallel live bake, ``moon_boot_bake_task``, 12288, MAX-3, core 1): it waits for the clock sync, bakes the now-frame into the shared ``s_hourly_gray`` bounce and sets ``s_boot_bake_done`` (:doc:`the boot movie <moon-movie>`). Then ``moon_replay`` (the producer, 12288, MAX-3, core 1): it plays the flash movie in a loop or the card fallback and publishes the final now-frame. If the producer fails to start, the code falls through to "baking the current phase inline" (the parallel bake is aborted and does not publish). Tick: ``moon_hourly_service`` at the tick head (``moon_ui.c:2502``; the probe gates on ``hour != s_baked_hour`` or ``CONFIG_TEST_HOURLY_FORCE_S``) spawns ``moon_hourly`` at the hour centre ``hour*3600+1800``. ``moon_rebake_request`` spawns ``moon_rebake`` from three tick callers: the degraded hourly probe ``:1805`` (the PSRAM bounce was never allocated), ``moon_ui_day_tick`` ``:2905`` (a new day or a changed sky-event signature) and the wake path ``:2979`` (a frame was mid-bake at sleep). The P4 branch overrides only ``task_stack_size = 12288``, so the P4 painter keeps ``ESP_LV_ADAPTER_DEFAULT_TASK_CORE_ID`` (-1, "(-1 for no affinity)", ``managed_components/espressif__esp_lvgl_adapter/include/esp_lv_adapter.h:33/:46``, vendored: public API cited, never modified). The dashed arrows are hand-backs, not calls: the producers publish into the shared PSRAM buffers, and the tick's done leg "shows it as ONE atomic whole-panel flush"; the classify worker warms the day that the tick reads later. Two tasks are drawn as ghosts: ``moon_orbit`` runs only with ``CONFIG_UI_MOON_ORBIT_ENABLE``, unset in both ``sdkconfig`` (``:2381``) and ``sdkconfig.p4`` (``:2512``), and ``hang_watch`` (``main.cpp:748``, 6144, prio 5) sits under ``CONFIG_TEST_HANG_WATCH`` and is never shipped.

.. graphviz:: ../_graphs/task-roster-ondemand.dot
   :alt: The on-demand tasks opened: loading the Wi-Fi screen starts wifi_sync and the Wi-Fi scan, an alarm ring or preview starts play_task, and each one-shot returns a done flag before it deletes itself.
   :align: center
   :caption: The on-demand tasks. Loading the Wi-Fi screen (``components/ui/actions.c:885``; the "on-demand sync" log line at ``:893``) arms the panel traffic hold first; then ``wifi_sync_kickoff`` spawns ``wifi_sync`` (6144, prio 10, unpinned: "6144: wifi_manager_init + the SNTP stack run here"). ``wifi.c:551`` ``wifi_scan_begin`` spawns ``wifi_scan`` (6144, prio 5). ``alarm_audio.c:337/:366`` spawn ``play_task`` (6144, prio 4) from ring_start and from the settings preview, one at a time. All of them delete themselves (``vTaskDelete(NULL)`` at the function tails), and ``wifi_sync`` sets the done flag that "the wake handler (action_wake_screen) consumes (clears)" (``actions.c:46`` comment).

.. graphviz:: ../_graphs/task-roster-knob.dot
   :alt: The knob's tasks opened: app_main spawns the painter knob_lvgl plus the boot, encoder, dial, power, moon and swap tasks; the producer's back buffer flows through the swapper into the painter, and the encoder feeds the dial.
   :align: center
   :caption: The knob's tasks. The renderer has the highest priority (10 > 7 > 5). ``knob_lvgl`` (p10, 8 KiB, unpinned) runs the painter, the input device and the 1300 ms software clock tick in one task. ``start_tasks`` spawns ``knob_moon`` (p5, core 1, 12 KiB internal RAM: the ``WithCaps`` call asks ``MALLOC_CAP_INTERNAL``, "unregistered from TWDT", ``knob_ui.c:757``) and ``knob_swap`` (p7, 8 KiB, registered: "a watchdog on it is a feature"). ``encoder_init`` spawns ``knob_enc`` (p5, 3 KiB). It polls the dial every ``ENC_POLL_MS`` 3 (``knob_board_encoder.c:32``), but ``CONFIG_FREERTOS_HZ=100`` makes ``pdMS_TO_TICKS(3)`` = 0, so the clamp ``poll < 1 ? 1 : poll`` lands it at one tick, 10 ms in effect. ``dial_start`` spawns ``knob_dial`` (p5, 4 KiB, 20 ms step plus the dim consumer). ``power_init`` spawns ``knob_power`` (p4, 3 KiB, the 500 ms sleep ladder, EXT0 dial and EXT1 tap on GPIO 9). The one-shot ``knob_boot`` (p6, 16 KiB; it carries the NTP window and the KNOB_UI_LAST_FRAME warm read and whole-panel arm) waits for NTP and arms. The knob has no IMU (the ``imu`` shim answers ``ready=false``) and no RTC; the 1300 ms tick is synthesized in software ("RTC_POLL_INTERVAL_MS on the clocks is 1300", ``knob/main/main.c:525-526``).

.. graphviz:: ../_graphs/task-roster-chips.dot
   :alt: The silicon opened: the painter queries the panels and touch chips every 33 ms, the IMU once a second, the RTC every 1.3 s; the bake tasks read the card, wifi_sync talks to the remote C6 over SDIO, play_task drives the codec, and the knob tasks read the dial and the battery ADC.
   :align: center
   :caption: The silicon: every periodic query with its cadence on the wire (:ref:`the silicon ladder <silicon-ladder-perf>` numbers them). The LVGL input layer reads the touch chip every ``LV_DEF_REFR_PERIOD`` = 33 ms (``lv_indev.c:105`` ``read_timer``; 33 in all three ``sdkconfig`` files). The 1 Hz ``lv_timer`` tilt monitor queries the QMI8658C (the chip streams at a 1000 Hz output rate, so one query per 1000 samples). The clock tick reads the PCF85063A at 0x51 and the TCA9554's INT pin (``EXIO_RTC_INT_PIN`` = ``PIN_NUM_3``) every ``RTC_POLL_INTERVAL_MS`` 1300 (``main.cpp:30``; the P4 leg is compiled out by ``CONFIG_CLOCK_USE_PCF85063A=n``). The hourly and replay producers read the microSD card (S3 GPSPI3 sdspi, P4 4-bit SDMMC). ``play_task`` streams to the ES8311 (I2C control and I2S, only while ringing). The P4's boot and on-demand sync crosses SDIO to the remote C6. ``CO5300``, ``SH8601`` and ``JD9365`` are the three panels of the family. The knob's CST816S INT line (GPIO 9) is wired but not polled: it is the EXT1 tap wake. The battery ``lv_timer`` reads ADC1_CH0 (GPIO 1) every 60 s (``knob_battery_ui.c:204``).

The buffers and who touches them
--------------------------------

Four pieces of shared memory and two keys carry a frame.
A producer bakes the moon in gray into the PSRAM bounce ``s_hourly_gray`` (8-bit, 160 KB), which no key guards.
It then publishes in one guarded block: a copy into ``s_frame`` (the complete gray frame) and a colour conversion into ``s_rgb`` (RGB565, 320 KB).
``s_rgb`` is the only buffer the screen reads.
The relief gradient cache is the 4 MB array of terrain slopes that every bake reads.

All moon-pipeline buffers are in PSRAM (``MALLOC_CAP_SPIRAM``):

.. list-table::
   :header-rows: 1
   :widths: 16 14 70

   * - Buffer
     - Size (S3, 400 px disk)
     - Writers / readers
   * - ``s_hourly_gray`` (bounce)
     - 400×400 × 1 B = 160 KB
     - the producers write it (TF hour fetch or the bake) outside both
       locks; the publish block copies it into ``s_frame``
   * - ``s_frame``
     - 400×400 × 1 B = 160 KB
     - written only by the whole-frame boot or day-tick bake (inside the image key)
       and by the producers' publish ``memcpy``; read by the LUT convert
       (``moon_fill_rgb_from_frame``, also used by the observer re-gather on a zone change)
   * - ``s_rgb``
     - 400×400 × 2 B = 320 KB
     - the LUT convert writes it (RGB565, one gather from
       ``s_frame``); LVGL reads it as the moon image
       (``s_dsc.data``)
   * - relief gradient cache
     - 2048×1024 × 2 B = 4 MB
     - built once at boot (``moon_relief``), read by every bake

Two locks order all of it:

.. list-table::
   :header-rows: 1
   :widths: 16 30 54

   * - Lock
     - Serializes
     - Held for
   * - ``s_rgb_mtx``
     - every reader and writer of ``s_rgb`` and ``s_frame`` (the
       whole-frame boot bake, the publish block, the orbit row sweep,
       the static LUT convert, the atomic ``lv_refr_now``)
     - one guarded block: a boot bake, a publish memcpy and gather, a
       LUT gather, or the whole-panel flush
   * - ``s_panel_mtx``
     - every QSPI or DSI panel transaction: LVGL flushes (through the
       ``draw_bitmap`` wrapper), the 0x51 brightness writes (fade,
       dim, backlight)
     - one strip (~10 ms) or one 0x51 ramp step

``s_rgb_mtx`` answers "who may read the image"; ``s_panel_mtx`` answers "who may touch the panel bus".
They guard different resources on purpose, and the producers use the split.
The expensive leg (the TF hour fetch or the ~1.3 s bake) runs into the bounce with no key held.
A producer takes ``s_rgb_mtx`` only for the short publish block (the ``memcpy`` and the RGB565 gather) that replaces ``s_frame`` and ``s_rgb``.
The publish comment in the code states the contract: "s_rgb_mtx held; no flush here (the tick shows it)".
The tick then holds the image key for its one atomic ``lv_refr_now`` and takes the panel key strip by strip.
Because the expensive leg needs neither key, the next bake and the current flush run at the same time.

The boot and day-tick bake is the one shape that holds the image key across the whole bake.
It writes ``s_frame`` directly, and at boot no frame is in flight whose painter could be kept waiting.

How a frame is handed over
--------------------------

.. graphviz:: ../_graphs/buffers-relay.dot
   :alt: The hand-over in sealed boxes: the core-1 bake writing the bounce without a lock, one publish block under the image key, the atomic show, and every panel write passing the panel key through the wrapper.
   :align: center
   :caption: The hand-over from bake to screen; every quote is read from ``components/ui/moon_ui.c`` (and the relief builder in ``components/moon/esp/moon_esp_assets.c``). The core-1 producers bake into the bounce without a lock ("12 KB stack (the bake's era chain …), core 1 (IDLE1 unwatched), same shape as moon_rebake_task", :1670): the TF hour fetch or the ~1.3 s bake, with the write-through to the card on store (:1745). ``s_hourly_gray`` (:1157, the code's "fetch/bake bounce") is the buffer no key guards. ``s_frame`` is "NEVER the partial side" (:1329): only the publish memcpys write it (hourly :1756, the boot presentation's final now-frame :2259). The ghost box is the other shape: the boot and day-tick bake runs the whole bake inside the image key (``moon_convert_to_rgb`` :1486-1501). Its boot leg runs on the 3.5 KB main task (``moon_bake_epoch`` :1543), where a synchronous whole-panel ``lv_refr_now`` would overflow the stack ("the 3.5 KB stack rule", :1124, :1230), so the LVGL task paints it instead. The relief cache feeds the bake one packed cell per bake pixel. It is built once at boot from one 2 MB ``esp_partition_read`` ("vs ~4 s for 1024 row reads"), and the transient buffer is freed: "steady-state relief memory is the one 4 MB packed array alone" (``moon_esp_assets.c:85-88``). ``s_rgb`` is the only buffer the screen reads (through ``s_dsc.data``, :2869). The publish contract in one line: "``s_rgb_mtx`` held; no flush here (the tick shows it)" (:1752). The painter's legs open in the next sketch. The strip staging and the witnesses are in ``docs/_graphs/flush-path.dot`` (:doc:`the display pipeline <display>`), and the core's data flow into the bounce is ``docs/_graphs/moon-core-flow.dot`` (:doc:`the C core <core-c>`). The same buffers counted in pixels (bake square, panel square, one DMA transaction) are :ref:`the pixel ladder <pixel-ladder-perf>`.

.. graphviz:: ../_graphs/buffers-relay-painter.dot
   :alt: The painter's side opened: the done legs with the drain witness and commit-on-show, the lock-free compute watermark driving the seconds arc, and the 0x51 ramp that writes no pixels while the panel is black.
   :align: center
   :caption: The painter's side (the 50 ms tick, ``MOON_TICK_MS`` :185, armed :2871). The done legs read their flags at the tick head (``moon_hourly_finish`` :3389-98; the presentation show of movie and card frames and the final now-frame :2688-701; the deferred re-bake :2710-22). Each calls ``moon_invalidate_all`` (:1303), then one atomic whole-panel ``lv_refr_now`` inside the image key, then the drain witness. There are 8 call sites with 9 site names: "re-bake" (:1501), "boot replay final" / "boot replay" (:2701), "rebake" (:2722), "orbit batch" (:2754), "tz re-gather" (:3006), "hourly" (:3396), "switch fade-in" (:3578), "screen switch" (:3589). Commit-on-show: ``s_baked_hour`` and the event are set only after the flush landed (:3397-98). The compute watermark (``moon_bake_progress`` :1427-64) publishes ``s_compute_pub`` under the image key per ``MOON_COMPUTE_ROWS`` strip, 13 rows on the P4 and 10 on the S3 (:1345-47): "the mutex orders the watermark against the screens' reads" (:1445). The screens read it through ``moon_ui_compute_active`` / ``moon_ui_compute_progress`` (:1467-74) as plain flag reads that drive the seconds arc, with no key and no flush. The producer "NEVER touches the panel" (:1424): the old frame (or the boot dummy disc) stays up for the whole bake, and the finished frame lands as the done leg's one atomic flush. The no-pixel leg: the 0x51 ramp writes no pixels (the fade-in leg :3407-39). The code warns that "the flush strips take the panel mutex in the draw_bitmap wrapper (per strip — taking it here as well would self-deadlock: the mutex is non-reentrant and the wrapper IS the flush's panel path)"; that is why the ramp node holds the panel key and the atomic flush lands while the panel is still black. The knob builds none of this: ``moon_ui.c`` is excluded wholesale (component ``ui``'s ``CMakeLists.txt:31-35`` empties ``UI_BOARD_SRCS`` under ``KNOB_BOARD``), and its seam has its own double buffer ``s_buf[2]``, 2×360²×2 B (``knob_moon_render.c``).

.. _guards-arch:

The guards in one picture
-------------------------

The hand-over above says who may hold which key, and when.
This set of figures counts the keys.
Every synchronization object the app owns is a node, every take or wait site is an edge labelled with its timeout, and every lock-free hand-off is a dashed edge.
``docs/tools/gen_perf_locks.py`` re-checks the whole census when it draws the figures and fails if any count changes:
three mutex creations, two event groups, one task-notify pair, six SEQ_CST fences, and zero ``xQueueCreate`` calls in app code.

.. graphviz:: ../_graphs/guards.dot
   :alt: The synchronization census at the top, feeding three sealed boxes: the clocks' two keys, the knob's one wrapped key, and the lock-free hand-offs.
   :align: center
   :caption: The guards in sealed boxes: the census at the head, three ownership stories below. Every edge was read from the live source (comment-stripped greps, as for the crew sketch). The same census as bars is :ref:`the guard ladder <guard-ladder-perf>`; the cadence of the queries is :ref:`the silicon ladder <silicon-ladder-perf>`; the tasks that hold the keys are :ref:`the crew sketch <task-roster-arch>`; the order of the two keys is the hand-over in the section above. Each box opens into its own sketch.

.. graphviz:: ../_graphs/guards-keys.dot
   :alt: The clocks' two keys opened: nine asking sites with their timeouts on the edges to the image key and the panel key, a one-way dashed order between the keys, and the watchdog probe in red.
   :align: center
   :caption: The clocks' two keys (S3 and P4, one ``moon_ui.c``), as ``moon_ui.c:2669-76`` declares them, created together at :2677-2678; the draw_bitmap wrapper is installed at :2690. Every edge carries its timeout. All eighteen image-key takes are ``portMAX_DELAY`` ("a publish block never fails": a publish block is a memcpy and a gather, never a bus wait). The nine panel-key takes are the app's whole timeout policy: the wrapper :704 waits forever; the two 0x51 legs ``fade_ramp`` :3139 and ``brightness_set`` :3236 wait forever (the wrapper's BSP delegate ``s_orig_draw`` is at :711); the traffic-hold quiesce :554 is bounded at 500 ms; ``moon_flush_wait_cb`` :475 is bounded at 2 s ("observed quiescence only", :476), and the witness arm takes the key at :894; the drain verify :1045 waits forever; the watchdog probe :2580 is bounded at 3 s. The S3 builds all nine; the P4 builds eight, because ``drain_verify`` is ``#if !defined(CONFIG_BOARD_P4)``. The dashed grey edge is the one-way order: no panel-key take ever nests an image-key take. ``moon_ui_screen_switched``, the one function that needs both (:3280), takes them one after the other: panel :3316, give, then rgb :3343. The drain verify holds the panel key across the whole submit-and-witness window ("no drain call site holds it", :1010). The hold measures about 50 ms: one 30 ms grace, then the immediate heal, which is the normal path behind a multi-strip flush (:1026-1103). A completion that was accepted but never signalled heals in place through ``tx_param(io, -1)`` (:1103, "runs the driver's own drain loop over ALL" :1090). The red watchdog is the last resort. It is created at :2719 (prio 3, core 0: "Low priority on core 0: it sleeps 3 s at a time" :2715), its take is "the ONLY work" (:2571), and on timeout it names the holder through ``xSemaphoreGetMutexHolder`` and restarts with "PANEL IO STUCK" (:2601-05). The strip staging and the witnesses are in ``docs/_graphs/flush-path.dot`` (:doc:`the display pipeline <display>`).

.. graphviz:: ../_graphs/guards-knob.dot
   :alt: The knob's single key opened: sixteen wrapper entries in three grouped nodes funnel into a holder-and-depth wrapper and its one physical take on s_lvgl_mux, with a depth self-loop.
   :align: center
   :caption: The knob's one key. ``s_lvgl_mux`` is created in knob ``main.c:465`` and handed into the seam at :566 (``knob_ui_set_lvgl_lock``). ``knob_ui.c`` owns the only physical take, :508 (``portMAX_DELAY``), inside a holder-and-depth wrapper :499, because "a FreeRTOS mutex is NOT recursively takable by its owner" (:492-93). A re-entry from the same task is ``s_lvgl_depth++`` (:505), and an unlock from a task that does not hold the key logs and is ignored (:517-18). The fifteen entries, across four files: 7 inside ``knob_ui.c`` (the last-frame lander's flush :439, the swap task's apply :951, ``invalidate_all`` :1074, the [BF] probe :1152, ``show_no_time`` :1182, ``show_moon``'s caption :1228/:1234); five in knob ``main.c`` (the painter's park loop :178, held across ``lv_timer_handler`` for 10–20 ms, the transient Wi-Fi arm :290, the two modem-sleep swaps :314/:361, the alarm wake :426); the dial commit ``knob_dial.c:100``; and the seam's two legs ``knob_moon_render.c:270/:335`` (refresh and home dim). The knob has no panel key: ``moon_ui.c`` is excluded wholesale (component ``ui``'s CMakeLists empties ``UI_BOARD_SRCS`` under ``KNOB_BOARD``), and the seam that implements ``moon_ui.h`` has no mutex at all; its ``moon_ui_init`` is a shim (``knob_moon_render.c:176``).

.. graphviz:: ../_graphs/guards-flags.dot
   :alt: The lock-free hand-offs opened: the SNTP and Wi-Fi event groups with their bounded waits, the six SEQ_CST fences, the one task-notify doorbell, the zero-queue check and the two vendored locks.
   :align: center
   :caption: The hand-offs that need no key. ``s_sntp_event_group`` (``clock_sntp.c:136``) sets ``SNTP_SYNC_BIT`` :120 and waits at :176 for a time the caller chooses: the clocks' boot fix and wake fix both pass ``CONFIG_CLOCK_NTP_TIMEOUT_MS`` 10000 (``clock.c`` and ``actions.c``), the knob's boot fix 20000 with a 2 s retry (``main.c``). ``s_wifi_event_group`` (``wifi.c:284``) sets CONNECTED :183 or FAIL :171 and waits at :329 for 8 s (``WIFI_CONNECTION_TIMEOUT_MS``). The six SEQ_CST fences are the knob's lock-free publish: the producer's row fence ``knob_moon.c:136`` and the ``s_pub`` fences ``knob_ui.c:619`` (the boot movie's publish), :920 (bake publish) and :959 (disc publish) pair with the swap reader's ``knob_ui.c:1256`` ("pair with the producer's publish fence"), plus the watermark owner-clear fence :907. The app's one task-notify pair is the day-scan doorbell: ``moon_event_classify_here`` calls ``xTaskNotifyGive(s_event_task)`` at ``moon_ui.c:1295``, and ``moon_event_task`` blocks on ``ulTaskNotifyTake(pdTRUE, portMAX_DELAY)`` at :1269. App code calls ``xQueueCreate`` zero times; the vendored BSP's transaction queue is cited, not ours: ``CONFIG_BSP_LCD_TRANS_QUEUE_DEPTH`` = 1 (S3 header, used at the BSP's :412; the 32 KiB SPI DMA rule). The adapter's LVGL mutex is vendored too, and the app takes it exactly once, ``main.cpp:579`` ``bsp_display_lock(-1)``.

The atomic-frame invariant
--------------------------

The screen never shows work in progress.
A frame is either not yet shown or fully shown, never half drained.
LVGL's default behaviour is the danger: it repaints asynchronously in 25-row strips, top to bottom, over about a second, so any moment inside a screen switch would show a mix of old and new.
The fix is one synchronous burst: a single ``lv_refr_now`` while holding the image lock.

Every moon-pipeline frame reaches the panel as one atomic whole-panel display.
On the LVGL path that means one ``lv_refr_now`` under ``s_rgb_mtx`` (:cfunc:`moon_ui_screen_switched`).
The new screen (moon image, labels, seconds arc) is rendered into LVGL's draw buffer and flushed to the panel in one synchronous burst, so a strip from the old screen and a strip from the new one never coexist.
Without the rule, a screen switch would wipe down the panel over a second; with it, a switch looks like a switch.

There is one exception, and it is a property of the panel, not of LVGL: the hardware brightness ramp (CO5300 ``0x51``, or the P4 backlight PWM), which writes no pixels.
Two flows use it:

1. The fade-to-black screen switch (``UI_SCREEN_SWITCH_FADE``).
   The switch ramps the panel's hardware brightness (CO5300 command 0x51) to 0, writing no pixels during the dim.
   It then runs the one atomic flush while the panel is black and ramps the brightness back.
   The atomic flush still happens; it happens while nothing is visible (:doc:`the animation pipelines <animations>`).
2. The light-sleep dim (the same ramp to the sleep floor and back).
   Entry ramps the panel down to ``UI_LIGHT_SLEEP_DIM_PCT`` with the static dim frame up, and a wake or an alarm ring ramps it back.
   Neither leg writes pixels (:doc:`the clock and sleep machine <clock-sleep>`).

A frame being computed is not an exception.
The panel keeps the image it already has (the old frame, or the quick phase disc where nothing is committed yet), and the seconds arc shows the bake's rows-completed watermark as a progress bar.
Every image that lands is one atomic whole-panel flush (:doc:`the compute progress <compute-progress>`).

Everything else that shows moon content obeys the rule.
At boot the quick phase disc lands in one atomic flush.
Each frame of the looping boot movie (the synodic cycle stored in flash, :doc:`the boot movie <moon-movie>`) lands in one atomic flush.
The finished current-phase bake lands in one atomic flush over the last movie frame.
The movie frames are read off-panel on core 1 and use the same atomic show path as the card replay and the hourly refresh.
The orbit re-bake, when enabled, lands its whole converted frame in one atomic flush.
The home moon is plain LVGL, shown static; every moon frame takes the atomic ``lv_refr_now`` path.
Several earlier pipelines wrote to the panel in other ways; they are gone, and :doc:`/project/history` records them.

Why the invariant is worth two locks
------------------------------------

A torn frame does not crash anything, which is what makes it dangerous.
The result would be the top 200 rows of one phase over the bottom 200 rows of the previous one.
Both halves are real moon pixels, so the image looks plausible and nothing in the logs complains.
On a moon-phase clock, showing the wrong moon is a broken product.
The locks turn "the screen shows a consistent image" from a race into a guarantee.

The rule costs little while the pipeline is idle.
Its price is two mutexes, a ``draw_bitmap`` wrapper on the panel (:doc:`the display pipeline <display>`), and a watchdog task that restarts the device if a panel transaction ever waits forever on the QSPI bus lock (:doc:`the display pipeline <display>`).

Where to go next
----------------

- :doc:`The C core <core-c>`: what a bake computes, and why the same code runs on the host.
- :doc:`The display pipeline <display>`: how a frame crosses the 32 KiB DMA path, and the guards on the panel bus.
- :doc:`The animation pipelines <animations>`: the hourly refresh, the boot presentation and the fade, built on these rules.
