System architecture
===================

The science section described what the renderer computes. This section describes where
each computation runs, who owns which buffer, and which rules keep a
240 MHz dual-core microcontroller with a 32 KiB DMA budget from tearing
an image in half on screen. The architecture has three properties
worth stating up front, because every design decision below serves
them:

1. **One image buffer, one atomic flush.** Every moon-pipeline frame
   reaches the panel as a single whole-panel display under one
   critical section. There is exactly ONE sanctioned exception,
   documented here: the hardware brightness ramp (which writes no
   pixels — the screen-switch fade; the once-per-hour refresh stopped
   riding it on :commit:`ee4e3efd`, the compute-progress change). The bake
   reveal's second exception (its *atomicity-per-state* band states)
   was retired the same day: the computing frame now shows as the
   quick phase disc + the seconds-arc progress bar, and every image
   that lands stays one atomic flush (:doc:`the compute progress
   <compute-progress>`).
2. **The physics is portable.** The moon core — ephemeris, bake,
   relief, rotation, the LUT — is a dependency-free C library that
   compiles unchanged into the device *and* the host simulator.
   Everything device-specific sits above it: the board shims, the LVGL
   UI, the panel's direct-write path.
3. **The display is the sleep surface.** The same pixels the clock
   shows while awake are what the light-sleep screen shows; the
   animations (:doc:`the animation pipelines <animations>`) are not decorative overlays — they are the
   transition itself.

The short version
-----------------

Everything on this page answers one question. The device has a single
screen, a complete moon image takes over a second to compute, and the
viewer must never catch the screen *mid-computation*. The four answers:

1. **Divide by knowledge, not by file count.** Each layer of the stack
   knows exactly one kind of thing: moon pixels, screens, minutes,
   directions, silicon. The moon math never learns it runs on hardware,
   which is why the identical source compiles into the device *and* the
   host simulator.
2. **Split the work across the two brains, and never let them share
   chores.** The ESP32 has two CPUs. Core 0 is the painter — it draws
   the screen, watches for touch, updates the clock, and must respond
   every 50 ms. Core 1 is the mill — it grinds out moon frames, ~1.3 s
   at a stretch. A bake that ever shared a core with the painter would
   stall the screen.
3. **Hand work off through two buckets and two keys.** Computed images
   live in two PSRAM buffers — a gray sketch and its colored print —
   and two mutexes decide who may touch what: one key for the image,
   one key for the panel's wires.
4. **One rule above all: the screen never shows work-in-progress.**
   Every frame arrives as a single atomic burst. The one sanctioned
   exception (a pure brightness ramp that writes no pixels) bends the
   letter, never the intent.

Everything below is the machinery behind those four sentences: the
layer stack, the task roster, the buffers and locks, and the invariant
itself.

Vocabulary
----------

Fourteen terms carry the page — the operating-system and hardware
basics are defined here, and the display-side ones also have fuller
entries in the :doc:`glossary <../glossary>`:

- **Task** — one independently-running program; the real-time OS
  (FreeRTOS) interleaves many of them so fast that they appear to run
  at the same time.
- **Core / pinning** — a physical CPU; the ESP32-S3 has two. *Pinning*
  a task binds it to one named core, which is how the design guarantees
  bakes and the painter never compete for the same CPU.
- **Mutex (lock) / critical section** — a key to a room: exactly one
  holder at a time, everyone else waits outside. A *critical section*
  is the stretch of code spent holding the key.
- **Atomic** — the viewer sees the whole thing or nothing; there is no
  observable half-state (glossary: *atomic frame*).
- **Flush / strip** — a *flush* copies a patch of pixels from RAM to
  the panel; panels like small pieces, so flushes go as horizontal
  *strips* (~25 rows on the S3). LVGL's default drain repaints a whole
  screen strip-by-strip, top-to-bottom, over about a second — the
  hazard the atomic rule exists to neutralize.
- **DMA** — direct memory access: a copy engine that moves bytes
  without CPU help. The S3's DMA carries at most 32 KiB per chunk and
  only reads internal RAM, which is why flush strips are staged there.
- **PSRAM** — the big RAM chip beside the ESP32: plenty for frame
  buffers, but reachable only through that limited DMA path.
- **LVGL** — the graphics library that keeps the tree of screens,
  labels and arcs and turns it into pixel writes.
- **Bake / producer** — the ~1.3 s computation of one complete moon
  frame from ephemeris and textures; the tasks that run it (orbit,
  rebake, hourly, the parallel boot bake) are the *producers*,
  publishing into the shared buffers.
- **LUT convert** — the pipeline's last step: a 256-entry look-up
  turns each 8-bit gray pixel into a 16-bit RGB565 color — one
  "gather" over the frame. With the observer view active it is the
  same gather *turned* by the Moon's parallactic angle for the
  timezone's reference city (``moon_rotate``) — the bake stays
  north-up and the tilt rides this one step
  (:doc:`the observer view <observer-view>`).
- **Relief gradient cache** — the 4 MB wall of precomputed terrain
  shading, built once at boot and read by every bake (glossary:
  *relief gradient cache*).
- **BSP / board shim** — the *BSP* (board support package) is the
  vendor's low-level driver for the panel chip; the *shim*
  (``components/board``) is our thin layer that makes an S3 and a P4
  look like one machine to everything above it.
- **Panel watchdog (``panel_wdg``)** — the guard task: if the panel key
  is not handed back within 3 s (a wedged bus), it restarts the device
  rather than leave a frozen screen up (glossary: *panel-io watchdog*).
- **0x51 brightness ramp** — the panel command (CO5300 ``0x51`` / the
  P4 backlight PWM) that dims or brightens the display while writing no
  pixels at all — which is why the fade flows fit inside the atomic
  rule's budget.

The layer stack
---------------

**The short version.** Picture a building where every floor can only
reach the one directly below it. Each floor owns one kind of knowledge:
``moon`` knows moon pixels, ``ui`` knows screens, ``clock`` knows
minutes, ``board`` knows the silicon, and the vendored BSPs know the
exact panel chips. Two facts make the project tractable: the moon core
has *no* dependencies at all (that is what "portable" means — the same
source compiles into the device and into the host simulator), and the
shim is the one place where "S3" and "P4" are different machines —
above it, every call site just says ``board_panel()``.

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

The stack reads top-down (each layer calls the ones below it; the
``components/moon`` core calls no layer at all — it is the portable,
dependency-free library :doc:`the C core <core-c>` walks). The per-file detail each
box summarizes lives in the pages that own that layer: the clock
files in :doc:`the clock and sleep machine <clock-sleep>`, the UI files in :doc:`the animation pipelines <animations>`, :doc:`the animation pipelines <animations>` and :doc:`the clock and sleep machine <clock-sleep>`, the core in
:doc:`the C core <core-c>`, the shim and BSPs in :doc:`the display pipeline <display>` and :doc:`the P4 port <p4-port>`.

The ``components/moon`` core is the only code that knows what a moon
pixel is. ``components/ui`` knows what a *screen* is. The clock
component knows what a *minute* is. The board shim is where "S3" (the Waveshare
ESP32-S3-Touch-AMOLED-1.75) and "P4" (the Waveshare
ESP32-P4-WIFI6-Touch-LCD-XC, "4C") stop being two codebases: every
call site sees
``board_panel()``/``board_display()``, and only the shim's two
implementations know which silicon is underneath.

The tasks
---------

**The short version.** The device always runs about five workers at
once, and the table below is their crew roster: who runs, on which of
the two CPUs, and what they own. One line explains the whole design:
the LVGL painter must answer every 50 ms, so every CPU-bound bake is
*pinned* to core 1 and runs as its own task — a 1.3 s bake can then
never stall the tick that drains the previous frame's strips. On the
S3 the painter's side of that split is enforced by the BSP itself (it
sets ``task_core_id = 0``); on the P4 the painter keeps the adapter's
default of *no affinity* and the split is carried by the producers'
pinning alone (:ref:`the crew sketch <task-roster-arch>` draws both).

The system runs on FreeRTOS. The load-bearing tasks and what core each
is pinned to:

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
     - boot sequence (spawns the boot presentation's two core-1 tasks
       — the parallel boot bake + the boot replay producer; with the
       replay off it bakes the static current phase itself),
       deep-sleep entry
   * - ``LVGL`` (adapter)
     - 0 on the S3; either on the P4
     - S3 yes (the BSP sets ``task_core_id = 0``); P4 no (the
       adapter default ``-1``, "no affinity", passes through)
     - the screen tree, the 50 ms ``moon_tick`` lv_timer, the
       minute-driven clock update, the fade/dim ramps
   * - bake tasks (orbit / rebake / hourly / boot bake)
     - 1
     - **yes** (pinned; internal-RAM stacks — ``pvPortMalloc`` is
       ``heap_caps_malloc(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)``;
       only the classify worker ``moon_event`` rides PSRAM via
       ``xTaskCreatePinnedToCoreWithCaps``)
     - ``moon_bake`` per frame into the shared PSRAM buffers;
       ``moon_rebake`` re-bakes the static frame off the LVGL task,
       ``moon_hourly`` produces the once-per-hour frame (a bake or a
       TF hour-cache fetch), ``moon_bake`` (the boot presentation's
       parallel live bake, :doc:`the boot movie <moon-movie>`) bakes
       the current phase IN PARALLEL with the looping movie — it polls
       for the clock sync first and sets ``s_boot_bake_done`` for the
       producer — and ``moon_replay`` (the producer) plays the
       **flash movie** looping (or the card's stored hours ascending
       as the fallback) and publishes the final now-frame — the tick
       displays each published frame atomically
   * - ``panel_wdg``
     - 0
     - yes
     - the panel-io watchdog (:doc:`the display pipeline <display>`): a 3 s-bounded test of the
       panel mutex, ``esp_restart()`` on overrun
   * - ``wifi_sync`` (background)
     - none
     - no
     - ``wifi_manager_init`` + SNTP sync off the LVGL task (the fix
       for the 1.7 s wake freeze, :doc:`the clock and sleep machine <clock-sleep>`)

Read the roster column by column. The **Core** column splits the crew:
the painter (core 0 on the S3 by the BSP's own line, either core on the
P4) and its watchdog on core 0, every bake producer on core 1, and
``wifi_sync`` pinned to *neither* — its only job is to do slow network
work out of everyone's way. The **Pinned?** column says ``yes`` exactly
where timing matters: the bakes must stay where they cannot starve the
tick. The **Owns** column holds one curiosity: ``panel_wdg`` owns a
watchdog whose entire job is to notice when somebody forgot to hand back
the panel key (:doc:`the display pipeline <display>`).

Two design choices are worth calling out. First, the bake producers
(orbit, rebake, hourly) are **pinned to core 1** because a bake
is CPU-bound work: the LVGL task (with the touch pipeline,
the clock minute update, and the fade ramps) must never share a core
with a bake or a gather. The S3 BSP enforces the painter's half of that
separation itself, quoting the design's failure mode verbatim: "The
adapter default (core -1, no affinity) lets FreeRTOS park it on core 1,
where the moon bake producer (pinned core 1, priority
``configMAX_PRIORITIES - 3``) starves it"
(``components/esp32_s3_touch_amoled_1_75/esp32_s3_touch_amoled_1_75.c:581-590``).
The P4's ``main.cpp`` passes ``ESP_LV_ADAPTER_DEFAULT_CONFIG()``
through — overriding only ``task_stack_size = 12288`` — so its painter
keeps the adapter default ``ESP_LV_ADAPTER_DEFAULT_TASK_CORE_ID``
(``-1``, "no affinity") and the producers' pinning alone carries the
split there. Second, those pipelines run as *tasks*
rather than on the LVGL task: a 1.3 s frame bake must not block the
50 ms tick that drains the previous frame's strips — the tick's only
pipeline job is displaying what the producers publish.

The **priority** this roster leaves implicit — the full ladder with
the knob rows, the band the ``esp_timer`` daemon shares with the
producers, and each producer's TWDT posture — is parsed and plotted in
:ref:`the scheduler ladder <scheduler-ladder-perf>`.

.. _task-roster-arch:

The crew in one picture
-----------------------

The crew is drawn in the bring-up style: one black-box overview, then a
detail sketch per sealed box — the clocks' resident crew, the on-demand
band, the knob's crew, and the silicon they all interrogate. The arrow
legend is the same on every sketch: **blue** is an ``xTaskCreate*``
spawn, **dashed grey** is a hand-back (done flag, published frame,
watermark), **amber** is a periodic chip ask.

.. graphviz:: ../_graphs/task-roster.dot
   :alt: The crew in black boxes — app_main, the sealed painter, the core-1 mill, the on-demand one-shots and the knob crew, each connected to the silicon box by cadence arrows
   :align: center
   :caption: The crew in black boxes — the roster table above says *who runs where*; this says **who spawns whom, who wakes whom, and which chip each hand asks**, with every box sealed. The three claims this crew holds up: (1) the painter's affinity is **per board** — the S3 BSP pins it to core 0 with its own comment (the quote is in the prose above), the P4 passes the adapter default through; (2) every stack is internal-RAM debt by definition — ``pvPortMalloc`` is ``heap_caps_malloc(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`` (``~/esp/esp-idf/components/freertos/heap_idf.c:43,:47``) — with ONE ``xTaskCreatePinnedToCoreWithCaps`` rider on ``MALLOC_CAP_SPIRAM``, ``moon_event``; ``knob_moon`` asks ``MALLOC_CAP_INTERNAL`` on the same call (:commit:`d7edb36f` — a task that can perform a flash write must never have its stack held up by the disabled cache, :doc:`/implementation/knob-display`); (3) the **static crew never exits and every dynamic member self-deletes** — and the two dashed ghosts are gated off in the shipped configs. The priorities of these same tasks are :ref:`the scheduler ladder <scheduler-ladder-perf>`, their resident bytes the :ref:`static footprint <static-footprint-perf>`, and the cadences on the amber arrows :ref:`the silicon ladder <silicon-ladder-perf>`. Each box opens below into its own sketch.

.. graphviz:: ../_graphs/task-roster-clocks.dot
   :alt: The clocks' crew opened — app_main and the LVGL worker spawn panel_wdg, moon_event, moon_replay, moon_hourly and moon_rebake, dashed hand-backs return to the painter, and the moon_orbit and hang_watch ghosts hang off it dashed grey
   :align: center
   :caption: The clocks' crew opened (S3 + P4, one source). The boot band: ``bsp_display_start`` runs the adapter's LVGL task (``main.cpp:558-572`` P4 branch, S3 falls through ``bsp_display_start``); ``moon_ui_init`` spawns ``panel_wdg`` (``xTaskCreatePinnedToCore`` "panel_wdg" 4096, prio 3, core 0 — "Low priority on core 0: it sleeps 3 s at a time") and the classify worker ``moon_event`` (``xTaskCreatePinnedToCoreWithCaps`` "moon_event" 12288, ``MAX_PRIORITIES-4``, core 1, ``MALLOC_CAP_SPIRAM`` — "IDLE1 is not WDT-watched, so its 15-20 s scan can never trip the watchdog"); ``moon_ui_start`` spawns the boot presentation's two core-1 tasks under ``#if CONFIG_UI_MOON_BOOT_REPLAY && !CONFIG_UI_DEMO_MODE`` (=y both clocks; ``sdkconfig:2359`` / ``sdkconfig.p4:2490``) — ``moon_bake`` FIRST (the parallel live bake, ``moon_boot_bake_task``, 12288, MAX-3, core 1 — it polls the clock sync, then bakes the now-frame into the shared ``s_hourly_gray`` bounce and sets ``s_boot_bake_done``; :doc:`the boot movie <moon-movie>`) then ``moon_replay`` (the producer, 12288, MAX-3, core 1 — plays the flash movie looping or the card fallback, publishes the final now-frame) — with the code's own fall-through "baking the current phase inline" when the producer fails to start (the bake is aborted; it will not publish). The tick band: ``moon_hourly_service`` at the tick head (``moon_ui.c:2502``; the probe gates on ``hour != s_baked_hour`` or ``CONFIG_TEST_HOURLY_FORCE_S``) spawns ``moon_hourly`` at the hour centre ``hour*3600+1800``; ``moon_rebake_request`` spawns ``moon_rebake`` from **three** tick callers — the degraded hourly probe ``:1805`` (the PSRAM bounce never allocated), ``moon_ui_day_tick`` ``:2905`` (new day or sky-event signature change) and the wake path ``:2979`` (frame mid-bake at sleep). The P4 branch overrides only ``task_stack_size = 12288``, so the P4 painter keeps ``ESP_LV_ADAPTER_DEFAULT_TASK_CORE_ID`` (-1, "(-1 for no affinity)" — ``managed_components/espressif__esp_lvgl_adapter/include/esp_lv_adapter.h:33/:46``, vendored: public API cited, never modified). The dashed arrows are hand-backs, not calls: the producers publish into the shared PSRAM buffers and the tick's done leg "shows it as ONE atomic whole-panel flush" / "the tick displays the finished frame atomically"; the classify worker warms the day the tick later reads. The two ghosts are drawn as ghosts: ``moon_orbit`` drains only with ``CONFIG_UI_MOON_ORBIT_ENABLE`` — unset in BOTH ``sdkconfig`` (``:2381``) and ``sdkconfig.p4`` (``:2512``) — and ``hang_watch`` (``main.cpp:748``, 6144, prio 5) sits under ``CONFIG_TEST_HANG_WATCH``, never shipped.

.. graphviz:: ../_graphs/task-roster-ondemand.dot
   :alt: The on-demand band opened — the WiFi screen load kicks off wifi_sync and wifi_scan_begin, the alarm ring or preview spawns play_task, and every one-shots' done flag returns to its spawner before it self-deletes
   :align: center
   :caption: The on-demand band opened. The WiFi screen load (``components/ui/actions.c:885``; the loaded-log "on-demand sync" at ``:893``) arms the panel traffic hold first, then ``wifi_sync_kickoff`` spawns ``wifi_sync`` (6144, prio 10, unpinned — "6144: wifi_manager_init + the SNTP stack run here"); ``wifi.c:551`` ``wifi_scan_begin`` spawns ``wifi_scan`` (6144, prio 5); ``alarm_audio.c:337/:366`` spawn ``play_task`` (6144, prio 4) from ring_start and the settings preview, one at a time. All self-delete (``vTaskDelete(NULL)`` at the function tails), and ``wifi_sync`` sets the done flag that "the wake handler (action_wake_screen) consumes (clears)" (``actions.c:46`` comment).

.. graphviz:: ../_graphs/task-roster-knob.dot
   :alt: The knob's crew opened — app_main spawns the painter knob_lvgl plus boot, encoder, dial, power, moon and swap, with the producer-marked back buffer flowing through the swapper into the painter and the encoder feeding the dial consumer
   :align: center
   :caption: The knob's crew opened — the renderer is the **foreground** (10 > 7 > 5). ``knob_lvgl`` (p10, 8 KiB, unpinned) carries painter, indev and the 1300 ms software clock tick in one task; ``start_tasks`` spawns ``knob_moon`` (p5, core 1, 12 KiB **internal RAM** — the ``WithCaps`` call asks ``MALLOC_CAP_INTERNAL``, "unregistered from TWDT", ``knob_ui.c:757``) and ``knob_swap`` (p7, 8 KiB, registered — "a watchdog on it is a feature"); ``encoder_init`` spawns ``knob_enc`` (p5, 3 KiB — it asks the dial every ``ENC_POLL_MS`` 3 (``knob_board_encoder.c:32``) but ``CONFIG_FREERTOS_HZ=100`` makes ``pdMS_TO_TICKS(3)`` = 0, so the clamp ``poll < 1 ? 1 : poll`` lands it at ONE tick = **10 ms effective**); ``dial_start`` spawns ``knob_dial`` (p5, 4 KiB, 20 ms step + dim consumer); ``power_init`` spawns ``knob_power`` (p4, 3 KiB, 500 ms sleep ladder, EXT0 dial · EXT1 tap on GPIO 9); the one-shot ``knob_boot`` (p6, 16 KiB — the NTP window and the KNOB_UI_LAST_FRAME warm read + whole-panel arm live on it; 4 KiB overflowed and the board cycled reboot→crash, bench 2026-11-21) waits for NTP and arms. The knob has **no IMU** (the ``imu`` shim answers ``ready=false``) and **no RTC** — the 1300 ms tick is software-synthesized ("RTC_POLL_INTERVAL_MS on the clocks is 1300", ``knob/main/main.c:525-526``).

.. graphviz:: ../_graphs/task-roster-chips.dot
   :alt: The silicon opened — the painter asks the three panels and the touch chips every 33 ms, the IMU at 1 Hz against its 1000 Hz stream, the RTC every 1.3 s; the mill asks the card, wifi_sync the remote C6 over SDIO, play_task the codec; the knob hands ask the dial and the battery ADC
   :align: center
   :caption: The silicon opened — every periodic ask with its cadence on the wire (the cadence ladder numbers them, :ref:`the silicon ladder <silicon-ladder-perf>`). The LVGL indev layer reads the touch chip every ``LV_DEF_REFR_PERIOD`` = 33 ms (``lv_indev.c:105`` ``read_timer``; =33 in all three ``sdkconfig``); the 1 Hz ``lv_timer`` tilt monitor asks the QMI8658C (the chip streams at 1000 Hz ODR — one ask per 1000 asks); the clock tick reads PCF85063A @0x51 + the TCA9554's INT pin (``EXIO_RTC_INT_PIN`` = ``PIN_NUM_3``) every ``RTC_POLL_INTERVAL_MS`` 1300 (``main.cpp:30``; the P4 leg is compiled out by ``CONFIG_CLOCK_USE_PCF85063A=n``); the hourly/replay producers interrogate the microSD (S3 GPSPI3 sdspi, P4 4-bit SDMMC); ``play_task`` streams the ES8311 (I2C control + I2S, rings only); the P4's boot/on-demand sync crosses the SDIO to the remote C6. ``CO5300``, ``SH8601`` and ``JD9365`` are the three panels of the family. The knob's CST816S INT line (GPIO 9) is wired but **not** a poll — it is the EXT1 tap-wake; the battery ``lv_timer`` asks ADC1_CH0 (GPIO 1) every 60 s (``knob_battery_ui.c:204``).

The buffers and who touches them
--------------------------------

**The short version.** Four pieces of shared memory, two keys. The
pipeline is a two-station paint shop: a producer sketches the moon in
grays into the PSRAM bounce ``s_hourly_gray`` (8-bit, 160 KB) — the
fetch/bake desk no key guards — and publishes it in one guarded block:
a copy into ``s_frame`` (the complete gray frame) plus a color print
into ``s_rgb`` (RGB565, 320 KB) — and ``s_rgb`` is the only buffer the
screen ever reads. The relief cache is a 4 MB wall of precomputed
terrain shading that every bake consults. The two mutexes keep the
stations from tripping over each other: one lock for the image, one
lock for the panel's wires.

The moon pipeline's buffers are all in PSRAM (``MALLOC_CAP_SPIRAM``):

.. list-table::
   :header-rows: 1
   :widths: 16 14 70

   * - Buffer
     - Size (S3, 400 px disk)
     - Writers / readers
   * - ``s_hourly_gray`` (bounce)
     - 400×400 × 1 B = 160 KB
     - the producers write it (TF hour fetch or the bake) *outside both
       locks*; the publish block copies it into ``s_frame`` and the bake
       reveal band-converts it into ``s_rgb``
   * - ``s_frame``
     - 400×400 × 1 B = 160 KB
     - only the whole-frame boot/day-tick bake (inside the image key)
       and the producers' publish ``memcpy`` write it; the LUT convert
       (``moon_convert_to_rgb``) and the heal paths read it
   * - ``s_rgb``
     - 400×400 × 2 B = 320 KB
     - the LUT convert writes it (RGB565, one gather from
       ``s_frame``); LVGL reads it as the moon image
       (``s_dsc.data``)
   * - relief gradient cache
     - 2048×1024 × 2 B = 4 MB
     - built once at boot (``moon_relief``), read by every bake

The two locks that order all of it:

.. list-table::
   :header-rows: 1
   :widths: 16 30 54

   * - Lock
     - Serializes
     - Held for
   * - ``s_rgb_mtx``
     - every reader/writer of ``s_rgb`` *and* ``s_frame`` (the
       whole-frame boot bake, the publish block, the orbit row sweep,
       the static LUT convert, the atomic ``lv_refr_now``)
     - one guarded block: a boot bake, a publish memcpy + gather, a
       LUT gather, or the whole-panel flush
   * - ``s_panel_mtx``
     - every QSPI/DSI panel transaction: LVGL flushes (via the
       ``draw_bitmap`` wrapper), the 0x51 brightness writes (fade,
       dim, backlight)
     - one strip (~10 ms) or one 0x51 ramp step

``s_rgb_mtx`` answers "who may read the image"; ``s_panel_mtx``
answers "who may touch the panel's io". They are deliberately
different resources, and the shipped producers exploit the split: the
expensive leg — the TF hour fetch or the ~1.3 s bake — runs into the
bounce with *no key held at all*, and the producers touch
``s_rgb_mtx`` only for the brief publish block (the ``memcpy`` plus the
RGB565 gather) that replaces ``s_frame``/``s_rgb``; the code's own
publish comment is the contract — "s_rgb_mtx held; no flush here (the
tick shows it)". The display path then takes the panel lock strip by
strip, and the next bake's expensive leg never waits on either key, so
the next bake and the current streaming flush run at the same time.
The boot/day-tick bake is the one shape that holds the image key
across its whole sketch (it writes ``s_frame`` directly, and at boot
there is no in-flight frame whose painter could be kept waiting).

Read the hand-off as a relay race with two keys: the baker's
*expensive* leg — fetch or bake into the bounce — runs outside both
keys; the baker's *publish* leg is one brief image-key block. The
painter's leg starts only after, holding the image key for its one
atomic ``lv_refr_now`` and taking the panel key strip by strip.
Keeping the expensive leg key-free is what lets the next bake and the
current streaming flush run at the same time, each trusting only its
own key.

The relay in one picture
------------------------

.. graphviz:: ../_graphs/buffers-relay.dot
   :alt: The relay in black boxes — the mill baking into the bounce lock-free, one image-key publish block, the atomic show, and every panel write riding the panel key through the wrapper
   :align: center
   :caption: The relay, drawn — every quote read out of ``components/ui/moon_ui.c`` (and the relief builder in ``components/moon/esp/moon_esp_assets.c``) at sketch time. The mill: the core-1 producers bake into the bounce lock-free — "12 KB stack (the bake's era chain …), core 1 (IDLE1 unwatched), same shape as moon_rebake_task" (:1670) — the TF hour fetch or the ~1.3 s bake, with the write-through to the card on store (:1745). ``s_hourly_gray`` (:1157, the code's own "fetch/bake bounce") is **the desk no key guards**; ``s_frame`` is "NEVER the partial side" (:1329) — written only by the publish memcpys (hourly :1756, the boot presentation's final now-frame :2259). The ghost box is the one **other** shape: the boot/day-tick bake runs the WHOLE bake inside the image key (``moon_convert_to_rgb`` :1486-1501 — the boot leg runs on the 3.5 KB main task (``moon_bake_epoch`` :1543), where a synchronous whole-panel ``lv_refr_now`` would overflow the stack ("the 3.5 KB stack rule", :1124, :1230), so the LVGL task paints it instead). The relief wall feeds the bake one packed cell per bake pixel, built once at boot from ONE 2 MB ``esp_partition_read`` — "vs ~4 s for 1024 row reads" — whose transient is freed: "steady-state relief memory is the one 4 MB packed array alone" (``moon_esp_assets.c:85-88``). ``s_rgb`` is the ONLY buffer the screen reads (the show reads it through ``s_dsc.data``, :2869). The publish contract in one line: "``s_rgb_mtx`` held; no flush here (the tick shows it)" (:1752). The painter's legs open into the sketch below; the strip staging, the accept-retry anatomy and the witnesses belong to ``docs/_graphs/flush-path.dot`` (:doc:`the display pipeline <display>`), and the core's data flow into the bounce to ``docs/_graphs/moon-core-flow.dot`` (:doc:`the C core <core-c>`). The same buckets counted in *pixels* — bake square, panel square, one DMA transaction — are :ref:`the pixel ladder <pixel-ladder-perf>`.

.. graphviz:: ../_graphs/buffers-relay-painter.dot
   :alt: The painter's legs opened — the done legs with the drain witness and commit-on-show, the key-free compute watermark driving the seconds arc, and the 0x51 ramp that writes no pixels while the panel is black
   :align: center
   :caption: The painter's legs opened (the 50 ms tick, ``MOON_TICK_MS`` :185, armed :2871). The **done legs** (flags read at the tick head → ``moon_hourly_finish`` :3389-98; the tick's presentation show — the movie / card frames and the final now-frame — :2688-701, and the deferred re-bake :2710-22) call ``moon_invalidate_all`` (:1303) then ONE atomic whole-panel ``lv_refr_now`` INSIDE the image key, then the drain witness — 8 call sites, 9 names: "re-bake" (:1501), "boot replay final" / "boot replay" (:2701), "rebake" (:2722), "orbit batch" (:2754), "tz re-gather" (:3006), "hourly" (:3396), "switch fade-in" (:3578), "screen switch" (:3589) — and commit-on-show: ``s_baked_hour``/event set only after the flush landed (:3397-98). The **compute watermark** (``moon_bake_progress`` :1427-64) publishes ``s_compute_pub`` under the image key per ``MOON_COMPUTE_ROWS`` strip — 13 rows P4 / 10 S3 (:1345-47) — "the mutex orders the watermark against the screens' reads" (:1445); the screens read the ``moon_ui_compute_active`` / ``moon_ui_compute_progress`` seam (:1467-74) as plain flag reads driving the seconds arc — no key owed on the read, no flush — and the producer "NEVER touches the panel" (:1424): the old frame (or the boot dummy disc) stays up the whole bake, and the finished frame lands as the done leg's one atomic whole-panel flush. The **no-pixels leg**: the 0x51 ramp writes NO pixels (the fade-in leg :3407-39) — the code's own warning, "the flush strips take the panel mutex in the draw_bitmap wrapper (per strip — taking it here as well would self-deadlock: the mutex is non-reentrant and the wrapper IS the flush's panel path)", which is why the ramp node rides the *panel* key and the atomic flush lands while the panel is still black. The knob builds NO relay at all: ``moon_ui.c`` is excluded wholesale (component ``ui``'s ``CMakeLists.txt:31-35`` empties ``UI_BOARD_SRCS`` under ``KNOB_BOARD``) and the seam answers with its own double buffer ``s_buf[2]`` — 2×360²×2 B (``knob_moon_render.c``).

.. _guards-arch:

The guards in one picture
-------------------------

The relay above sequences the keys — who may hold which, when. This
figure set counts them: every synchronization object the app owns is a
node, every take/wait site an edge with its timeout policy on the
wire, and every lock-free hand-off a dashed one. The whole census is
re-asserted at figure time by ``docs/tools/gen_perf_locks.py``, which
exits with an error the moment any count drifts: **three** mutex
creations, **two** event groups, **one** task-notify pair, **six**
SEQ_CST fences, and **zero** ``xQueueCreate`` calls in app code.

.. graphviz:: ../_graphs/guards.dot
   :alt: The guards in black boxes — the synchronization census feeding three sealed boxes: the clocks' two keys, the knob's one wrapped key, and the key-free band
   :align: center
   :caption: The guards in black boxes — the census at the head, three sealed ownership stories below. Every edge was read out of the live source at sketch time (comment-stripped greps, the same discipline as the crew sketch). The same census counted as bars is :ref:`the guard ladder <guard-ladder-perf>`; the cadence with which these hands are asked is :ref:`the silicon ladder <silicon-ladder-perf>`; the crew that holds the keys is :ref:`the crew sketch <task-roster-arch>`; the relay that sequences the two keys is the section directly above. Each box opens into its own sketch.

.. graphviz:: ../_graphs/guards-keys.dot
   :alt: The clocks' two keys opened — nine asker nodes with their timeouts on the ask edges to the violet image and panel keys, the one-way dashed order between the keys, and the red watchdog probe
   :align: center
   :caption: The clocks' two keys opened (S3 + P4, one ``moon_ui.c``) — the two keys exactly as ``moon_ui.c:2669-76`` declares them, created together at :2677-2678, the draw_bitmap wrapper landing at :2690. Every ask edge carries its timeout: all **eighteen** image-key takes are ``portMAX_DELAY`` ("a publish block never fails" — a publish block is a memcpy plus a gather, never a bus), while the **nine** panel-key takes are the entire timeout policy of the app — the wrapper :704 forever; the two 0x51 legs ``fade_ramp`` :3139 and ``brightness_set`` :3236 forever (the wrapper's BSP-delegate call — ``s_orig_draw`` — at :711); the traffic-hold quiesce :554 bounded 500 ms; ``moon_flush_wait_cb`` :475 bounded 2 s — "observed quiescence only" (:476), the witness arm's own take at :894; the drain verify :1045 forever; the watchdog probe :2580 bounded 3 s. The S3 builds all nine; the P4 builds **eight** — ``drain_verify`` is ``#if !defined(CONFIG_BOARD_P4)``. The dashed grey edge is the **one-way order**: no panel-key take ever nests an image-key take, and ``moon_ui_screen_switched`` — the one both-keys case (:3280) — takes them SEQUENTIALLY, panel :3316, give, then rgb :3343. The drain is the wedge story: rule 3 holds the panel key across the WHOLE submit+witness window ("no drain call site holds it -- audited 2026-11-13" :1010; the hold now measures ~50 ms -- one 30 ms grace then the straight-away heal, the NORMAL path behind a multi-strip flush :1026-1103); an accepted-but-silent completion heals in place via ``tx_param(io, -1)`` (:1103, "runs the driver's own drain loop over ALL" :1090). The red watchdog is the last resort: born at :2719 (prio 3, core 0 — "Low priority on core 0: it sleeps 3 s at a time" :2715), its take is "the ONLY work" (:2571), and on timeout it names the holder through ``xSemaphoreGetMutexHolder`` and restarts under "PANEL IO STUCK" (:2601-05). The strip staging and the witnesses belong to ``docs/_graphs/flush-path.dot`` (:doc:`the display pipeline <display>`).

.. graphviz:: ../_graphs/guards-knob.dot
   :alt: The knob's single key opened — sixteen wrapper entries in three grouped nodes funneling into the holder+depth wrapper and its one physical take on s_lvgl_mux, with a depth self-loop
   :align: center
   :caption: The knob's one key opened: ``s_lvgl_mux`` is created in knob ``main.c:465`` and handed into the seam at :566 (``knob_ui_set_lvgl_lock``); ``knob_ui.c`` owns the ONLY physical take :508 (``portMAX_DELAY``) inside a holder+depth wrapper :499 — "a FreeRTOS mutex is NOT recursively takable by its owner" (:492-93, the 2026-11-10 hub-tap TAP deadlock), so a same-task re-entry is ``s_lvgl_depth++`` (:505) and an unlock from a non-owner logs and is ignored (:517-18). The FIFTEEN entries, across four files: 7 inside ``knob_ui.c`` (the last-frame lander's flush :439, the swap task's apply :951, ``invalidate_all`` :1074, the [BF] probe :1152, ``show_no_time`` :1182, ``show_moon``'s caption :1228/:1234), knob ``main.c``'s five (the painter's park loop :178 held ACROSS ``lv_timer_handler`` (10–20 ms), the transient-WiFi arm :290, the two modem-sleep swaps :314/:361, the alarm wake :426), the dial commit ``knob_dial.c:100``, and the seam's two legs ``knob_moon_render.c:270/:335`` (refr + home dim). The knob builds **no panel key at all**: ``moon_ui.c`` is excluded wholesale (component ``ui``'s CMakeLists empties ``UI_BOARD_SRCS`` under ``KNOB_BOARD``) and the seam that answers ``moon_ui.h`` carries zero mutex tokens — its ``moon_ui_init`` is a shim (``knob_moon_render.c:176``).

.. graphviz:: ../_graphs/guards-flags.dot
   :alt: The key-free band opened — the SNTP and WiFi event groups with their bounded waits, the six SEQ_CST fences, the one task-notify doorbell, the zero-queue assertion and the two vendored hands
   :align: center
   :caption: The key-free band opened — the hand-offs the app runs without any key. ``s_sntp_event_group`` (``clock_sntp.c:136``) sets ``SNTP_SYNC_BIT`` :120 and waits :176 bounded BY CALLER — the clocks' boot fix and wake fix both pass ``CONFIG_CLOCK_NTP_TIMEOUT_MS`` 10000 (``clock.c`` + ``actions.c``), the knob's boot fix 20000 with a 2 s retry (``main.c``). ``s_wifi_event_group`` (``wifi.c:284``) sets CONNECTED :183 / FAIL :171 and waits :329 for 8 s (``WIFI_CONNECTION_TIMEOUT_MS``). The SIX SEQ_CST fences are the knob's key-free publish hand-off: the producer's row fence ``knob_moon.c:136`` and the ``s_pub`` fences ``knob_ui.c:619`` (movie publish — the boot movie's fence) / :920 (bake publish) / :959 (disc publish) pair with the swap reader's ``knob_ui.c:1256`` ("pair with the producer's publish fence"), plus the watermark owner-clear fence :907. The app's ONE task-notify pair is the day-scan doorbell: ``moon_event_classify_here`` rings ``xTaskNotifyGive(s_event_task)`` at ``moon_ui.c:1295`` and ``moon_event_task`` blocks on ``ulTaskNotifyTake(pdTRUE, portMAX_DELAY)`` at :1269. ``xQueueCreate`` fires ZERO times in app code — the vendored BSP's trans queue is cited, never ours: ``CONFIG_BSP_LCD_TRANS_QUEUE_DEPTH`` = 1 (S3 header, used at the BSP's :412 — the 32 KiB SPI DMA invariant). The adapter's LVGL mutex is VENDORED too — the app takes it EXACTLY once, ``main.cpp:579`` ``bsp_display_lock(-1)``.

The atomic-frame invariant
--------------------------

**The short version.** This is the rule the section is named for, in
one sentence: *the screen never shows work-in-progress.* A frame is
either not yet displayed or fully displayed — never half-drained. The
danger is LVGL's default manners: it repaints asynchronously, ~25-row
strips top-to-bottom over about a second, so any moment inside a switch
shows a hybrid of old and new. The fix is one synchronous burst — a
single ``lv_refr_now`` while holding the image lock. The one sanctioned
"exception" bends the letter, not the intent: the hardware brightness
ramp writes no pixels at all (the screen-switch fade and the light-sleep
dim run it while the current image stays up and consistent).

**Every moon-pipeline frame hits the panel as one atomic whole-panel
display.** On the LVGL path that means one ``lv_refr_now`` under
``s_rgb_mtx`` (``moon_ui_screen_switched``): the new screen — moon
image, labels, seconds arc — is rendered into LVGL's draw buffer and
flushed to the panel in one synchronous burst, so no strip from the
old screen and no strip from the new one ever coexist.

Without the invariant, LVGL's async 25-row strip drain repaints a
screen top-to-bottom over about a second — the "wake artifact" the
original design fought. The atomic rule is what makes a screen switch
look like a screen switch.

There is exactly **ONE sanctioned exception**, and it is a property of
the panel, not of LVGL: the **hardware brightness ramp** (CO5300
``0x51`` / the P4 backlight PWM), which writes no pixels. Two flows
ride it:

1. **The fade-to-black screen switch** (``UI_SCREEN_SWITCH_FADE``):
   the switch dims the panel's own hardware brightness (CO5300
   command 0x51) to 0, writes *no pixels at all* during the dim,
   performs the one atomic flush while the panel is still black, and
   ramps the brightness back. The invariant is preserved *off the
   visible path*: the atomic flush runs, it just runs while nothing is
   visible (:doc:`the animation pipelines <animations>`).
2. **The light-sleep dim** (the same ramp to the sleep floor and back):
   the entry ramps the panel down to ``UI_LIGHT_SLEEP_DIM_PCT`` while
   the static dim frame is up, the wake / an alarm ring ramps it back —
   no pixels written on either leg (:doc:`the clock and sleep machine
   <clock-sleep>`).

The second exception — the **bake reveal**'s *atomicity-per-state* band
states (:commit:`5008dade`) — was retired on :commit:`ee4e3efd` with the
**compute-progress** change (:doc:`the compute progress
<compute-progress>`): a frame being *computed* no longer displays as a
designed sequence of strip bands; the panel keeps the image it already
has (the old frame, or the quick phase disc where nothing is committed)
and the seconds arc sweeps the bake's rows-completed watermark as a
progress bar, so every image that lands is still ONE atomic whole-panel
flush and the ramp's riders are the only no-pixel flows left.

(The burn-in spin's direct panel write and the TF sweep cache's
direct-write replay were further exceptions until :commit:`79937e37`, when
the spin subsystem was removed entirely —
*"It does more things worse than good."* — and the boot sweep (the TF
replay's producer) followed later the same day. The TF cache lives on
as the hourly refresh's hour cache, and every moon frame now rides the
LVGL path.)

Everything else that shows moon content obeys the rule: the boot and
wake displays (one atomic flush of the quick phase disc at boot, then
one atomic flush of each looping **boot movie** frame — the flash-resident
synodic cycle, :doc:`the boot movie <moon-movie>` — and one atomic
flush of the finished current-phase bake over the last movie frame),
and the orbit re-bake (one atomic flush of the whole converted frame).
The movie re-sourced the boot presentation on :commit:`7693bffa`, and it adds
**no new exception**: its frames are read off-panel on core 1 and shown
through the very same sanctioned atomic-show path the card replay and
the hourly refresh ride. (The boot sweep's frames — card-less and warm
TF replay alike — and the dusk's re-projected frames had obeyed it one
flush per published frame until their retirement.)

Why the invariant is worth two locks
------------------------------------

**The short version.** Why take two mutexes and a watchdog for one
picture? Because the failure mode is not a crash — it is a *plausible
lie*: the top half of one moon over the bottom half of the next, every
pixel real, nothing in the logs to complain. On a moon-phase clock,
showing the wrong moon is a broken product. The locks are what turn
"the screen always shows a consistent image" from a hope into a
guarantee, and the rest of this section prices that guarantee.

The atomic rule is cheap when the pipeline is idle and expensive to
violate when it is not: the failure mode is not a crash, it is a
*half-screen frame* — the top 200 rows of one phase over the bottom
200 rows of the previous. That is precisely what a moon-phase clock
must never show, because the half-frame is *plausible*: both halves
are real moon pixels. The invariant is what makes "the screen shows
a consistent image" a theorem instead of a race.

The cost is two mutexes, a panel ``draw_bitmap`` wrapper (:doc:`the display pipeline <display>`),
and a watchdog task that restarts the device if a panel transaction
ever parks forever on the QSPI bus lock (:doc:`the display pipeline <display>`). The rest of
The implementation section is those mechanisms, one page each.
