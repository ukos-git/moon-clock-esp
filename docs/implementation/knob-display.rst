The knob display
================

.. brief:
   Reader: a firmware developer working on the knob board or on the shared components it links.
   They already know: C, FreeRTOS tasks and mutexes, LVGL basics, the clock's moon pipeline.
   After this page they can: trace how a moon frame, a tap, a dial turn and a sleep cycle run on the knob, and where its code differs from the clocks.
   Page type: explanation.

The Waveshare ESP32-S3-Knob-Touch-LCD-1.8 is the third board of the moon-clock family.
This page explains how the knob runs the family app: how its frames reach the panel, how it boots, keeps time and sleeps, and how the dial drives the shared screens.
The hardware side is :doc:`the Knob board <../devices/knob>`.

The short version
-----------------

The knob is an esp32s3 target of the same project, selected by ``-D KNOB_BOARD=ON`` (:doc:`the build system <../reference/build-system>`), and it compiles the clock's shared ``components/{clock, ui, wifi, moon}`` alongside its own ``knob_*`` components.
It answers the board-specific symbol surface with the knob's rows in the shared shims — ``components/board`` dispatching to the ``knob_board`` component, ``components/imu`` compiling its absent-IMU stub — the way the P4 port answers it with its BSP.
The family seam came in five steps (P0 family seam, P1 home parity, P2 dial-driven settings, P3 alarm editing, P4 battery gauge; :commit:`d96fdaff`).

- The app on glass is the family app.
  The shared home screen (moon image, split-flap HH:MM, alarm row, seconds arc) compiles from ``components/ui`` unchanged and runs through the `render seam <The render seam (P1)_>`_.
  The legacy own-screen view (one moon disk and a caption) still builds behind ``KNOB_UI_ENABLE=n``.
- The physics is the clock's, compiled once, and the knob image gate checks it.
- A producer task on CPU1 bakes frames; a swap task puts each finished frame on the panel in one atomic flush.
- The board has no RTC (real-time clock chip).
  It shows a phase only on SNTP truth or a saved NVS snapshot, never on a guess.
- A power ladder dims the backlight and then calls ``esp_deep_sleep_start``.
  Before sleeping it can arm the S3's internal RTC timer, so an alarm rule wakes the board.
- The dial is an input device: it steps a per-screen cursor, and a tap commits.

Some clock features have no counterpart on the knob:

- the 0x51 brightness ramp and the panel watchdog (the SH8601 is a backlit LCD with no burn-in exposure and no TE line);
- the IMU tilt logic (there is no IMU chip, so the shim answers fixed-upright);
- audio (there is no codec, so a silent stub answers ``alarm_audio.h``).

An alarm rule therefore never rings; it shows the alarm screen.
The alarm scheduler is the shared one.
It loads ``/spiffs/alarm.txt`` and arms its esp_timer while awake, and the deep-sleep arm extends it with the RTC TIMER wake (see `the power ladder`_).

How the knob runs, as one state machine
---------------------------------------

Every boot and every wake runs the same path.
Read the diagram top to bottom; each state links to the section that explains it.

.. raw:: html

   <pre class="mermaid">
   stateDiagram-v2
     [*] --> WarmArm: power-on, or EXT0 / EXT1 / TIMER wake
     WarmArm: warm arm in app_main (saved frame, else movie frame 0, else phase disc)
     WarmArm --> SyncWindow: ui_init, boot_arm_task holds the ladder
     SyncWindow: WiFi screen - join + NTP (20 s window)
     SyncWindow --> Home: SNTP fix, or NVS snapshot arm, or "no time"
     Home: home screen - fresh bake swaps over the frame, arc shows progress
     Home --> Dim: idle for idle_timeout_sec
     Dim: dim rung - LEDC 25 %, clock alpha 20 %, arc hidden
     Dim --> Home: touch, or brightening dial
     Dim --> Sleep: deep_sleep_countdown_sec later
     Sleep: deep sleep - EXT0 dial, EXT1 touch, optional RTC TIMER
     Sleep --> WarmArm: wake
     Sleep --> AlarmCheck: TIMER wake
     AlarmCheck: wake arbitration in main.c
     AlarmCheck --> Alarm: within the grace window
     AlarmCheck --> Alarm: early wake - hold dark, fire at the rule
     AlarmCheck --> WarmArm: stale fire
     Alarm: silent alarm screen
   </pre>

The states in words:

1. **Warm arm.**
   ``app_main`` stages the first frame before ``ui_init``, so the first screen flush already carries a moon (`the opening act`_).
2. **Sync window.**
   The arm loads the transient WiFi screen, joins, and waits for SNTP (see the SNTP section below).
   The ladder is held for the whole opening act, so a slow window never dims or sleeps a board that has shown nothing yet.
3. **Home.**
   The producer bakes the current phase while the seconds arc shows its progress; the swap task lands it in one flush (`the frame path`_).
   A 1 Hz day tick re-bakes when the UTC date or the sky event changes.
4. **Dim and sleep.**
   The 500 ms supervisor walks awake → dim → deep sleep on the sleep screens' settings (`the power ladder`_).
5. **Wake.**
   A dial or tap wake re-runs the boot path.
   A TIMER wake first goes through the alarm arbitration: it fires the alarm, holds dark until the rule, or treats the wake as stale.

The same physics, verified the same way
---------------------------------------

The physics is the clock's, compiled once.
The shared ``components/moon`` is the single provider of the six core and eleven vendored ERFA TUs, plus the two ESP device backends (``moon_esp_assets.c`` and ``moon_esp_movie.c``).
Its CMakeLists selects ``generated/knob`` under a ``CONFIG_BOARD_KNOB`` branch.
That branch comes first-match ahead of the P4/s3 branches, and the clock projects never define the symbol, so their TUs compile exactly as before.
``knob_moon`` is glue only.

Because of that, ``host_sim/.venv/bin/python host_sim/check_gate.py --target knob`` describes exactly the physics the panel shows.
The knob gate reads mean MAE 6.45 / SSIM 0.907 (s3 6.68/0.908, p4 6.73/0.907).
The stale-binary guard ties the sim to the knob's own baked textures (albedo 864×432 and DEM 1024×512, :doc:`the moon constants <../reference/moon-consts>`).
The DEM is the knob's only reduced asset.
The 1024×512 relief cache is 1 MiB resident plus 0.5 MiB transient instead of 4 + 2 MiB, and the gate accepted it (0 frames over the MAE ≤ 12 bar).

The frame path
--------------

The moon is one LVGL ``lv_image`` over a PSRAM (external pseudo-static RAM) RGB565 square.
It reaches the panel through the existing partial flush, which uses two 360×30 internal-DMA buffers.
Three tasks share the work.

The producer task bakes frames into a PSRAM double buffer.
It is pinned to CPU1 and not registered with the task watchdog (``xTaskCreatePinnedToCoreWithCaps``, the clock's bake-producer posture for its re-bake and hourly tasks).
Everything long on it yields:

- The core's classify scan runs on a 20 ms wall-clock budget (``moon_event.c``'s ``CONFIG_BOARD_KNOB`` branches cover the coarse grid and ``se_argmax``'s 73-sample fine window).
- The bake runs row by row.

So ``sdkconfig.defaults.knob`` keeps ``CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1`` on and sets ``CONFIG_ESP_TASK_WDT_PANIC``.
A wall-clock budget caps every stretch at about 20 ms plus one evaluation.
``full_moments`` logs its cost (``N evals in M ms``), once per calendar day through the cache.
The classify scan measures 463 ephemeris evaluations in 24.6 s (about 53 ms each at ``-Og``).
With this posture a boot runs the full scan and 110 s of presentation with 0 task-WDT and 0 resets.

The producer's stack lives in internal RAM, not PSRAM.
The same ``xTaskCreatePinnedToCoreWithCaps`` call passes ``MALLOC_CAP_INTERNAL`` and asks for 12 KiB.
The rule it encodes: a task that can write to flash must never be the running task whose stack the cache is holding up.
The producer can write to flash, because its day-tick path saves settings and every spiffs write drops both caches.
IDF asserts the condition outright (``esp_task_stack_is_sane_cache_disabled()`` in ``spi_flash_disable_interrupts_caches_and_other_cpu``).
The coredump and the bench repro that proved it are in :ref:`the field-crash witness <knob-crash-witness>`.

The swap task (an ordinary registered task) applies each frame as one atomic ``lv_refr_now`` under the main-injected LVGL mutex.
It uses ``seq_cst`` fences around publish and apply, and an ack-before-reuse cap: the producer never rewrites a buffer the swap has not acknowledged (5 s timeout).
The producer's bake publishes a rows-completed watermark per row (``knob_ui_compute_active`` / ``knob_ui_compute_progress``, ``knob_ui.c:974``).
The shared home's seconds arc reads it as the compute progress bar (:doc:`the compute progress <compute-progress>`).
The swap applies only the finished frame, so the partial rows of an in-flight bake never reach the panel.
There are no band flushes and no heal path.

The image descriptor declares plain ``LV_COLOR_FORMAT_RGB565``, and the draw unit byte-swaps into the ``RGB565_SWAPPED`` canvas.
A purple wash on glass would mean that swap is misconfigured.
The F1 fallback (one continuous window write against QSPI seam lines) was specified in the plan and not built, because the partial drain never showed seam lines on this panel.

Measured on the S3R8: relief build 727 ms, the boot bake 991 ms, about 6.8 MB PSRAM free after assets and buffers, 0 TWDT.
When no saved frame is on glass, the first ink is the quick phase disc (``disc_publish``, tens of ms, :doc:`the compute progress <compute-progress>`).
The seconds arc sweeps the per-row watermark while the roughly 1 s bake runs behind it.
The earlier bench figures (the boot sweep and the band-display reveal) are in :doc:`/project/history`.

The render seam (P1)
--------------------

The shared ``components/ui`` compiles into the knob unchanged.
That covers the six core TUs (``ui.c``, ``screens.c``, ``images.c``, ``styles.c``, ``actions.c``, ``vars.c``) and the generated ``fonts_knob/`` tier.
The tier's sizes are 74/37/25 px, the S3's 96/48/32 baselines scaled by 360/466.
Like ``fonts_p4/``, the six file names keep the baseline sizes and the baked sizes live inside the files.
The provenance (``lv_font_conv``) and the symbol names are the same, so ``fonts.h`` never changes.
The regular icon face is the one exception: it is baked from the P4's set, so the knob carries the clock U+F017 it never draws (:ref:`the type sketch <type-family-ui>`, :ref:`the glyph ladder <glyph-ladder-perf>`).

The four AMOLED- or board-coupled TUs (``moon_ui.c``, ``moon_tfcache.c``, ``alarm_audio.c``, ``demo.c``) drop out of the knob's SRCS.
The knob answers their public headers from its own components.

``knob_moon_render.c`` implements the ``moon_ui.h`` seam: the eighteen functions the shared screens and actions call.

- ``moon_ui_tz_changed`` is a deliberate no-op (:commit:`d177a8fb`).
  The knob's viewport stays fixed north-up.
  The TIMEZONE screen still applies and persists its zone through the clock component; only the moon's disk tilt is not adopted (:doc:`the observer view <observer-view>`).
- Fades and dim ramps become LEDC duty ramps (10 ms steps on GPIO 47).
  There is no 0x51 QSPI brightness path to wedge, and the knob never sends 0x51.
- ``moon_ui_screen_switched`` is the one atomic whole-panel ``lv_refr_now`` under the injected LVGL mutex, the family's hard invariant.
  The knob has exactly two ``lv_refr_now`` owners: this seam and the swap task.
- ``moon_ui_create_image`` attaches through the producer/swap core's shared PSRAM descriptor.
- ``moon_ui_day_tick`` is a documented no-op.
  The classify and event probe is the producer task's job on CPU1, never the LVGL tick's (the S3's minute-freeze lesson, :doc:`the animations page <animations>`).
- Tilt reports settled.
  The seam has no spin, panel-step, keep-mask or dusk entries (:commit:`79937e37`).

``knob_alarm_audio_stub.c`` answers ``alarm_audio.h``.
Ring and preview are silent no-ops, and every selectable sound is named "Silent" because the board has no codec.
The shared alarm paths link and run.

The ``board``/``imu`` shims (P0) stay, plus a silent name-shim component ``esp_codec_dev``.
The ui component's REQUIRES must be unconditional, because the component manager's early requirements pass evaluates component CMakeLists with neither project variables nor ``CONFIG_`` symbols.
(``build.cmake``'s ``__component_get_requirements`` runs ``cmake -P`` on the properties files only.)
For the same reason the knob branches there key on the project variable ``KNOB_BOARD``, never on ``CONFIG_BOARD_KNOB``.
The seam pictures (overview, projects, passes, shims, guards) are drawn in :doc:`the build system <../reference/build-system>`.
The size of the seam is counted in four ladders:

- the shims compile to 16 + 28 + 0 lines against the 5,178 the build drops (:ref:`the build census <census-ladder-perf>`);
- the knob's menu has 85 entries, the biggest on the bench, and 29 of its 52 inherited entries are clock-private (:ref:`the configuration ladder <config-ladder-perf>`);
- the knob's fifteen own files compile exactly once each, in the largest repo-owned image of the family (59 TUs) with the least reuse (:ref:`the sharing ladder <sharing-ladder-perf>`).

Why the LVGL mutex is reentrant
...............................

The injected mutex is the app-wide ``s_lvgl_mux`` that ``lvgl_task`` holds across ``lv_timer_handler``, and LVGL event handlers run inside that context.
The home tap's chain (``CLICKED`` → hub ``loadScreen`` → ``moon_ui_screen_switched`` → ``knob_ui_invalidate_all``) takes it again from the task that already owns it.
A FreeRTOS mutex is not recursively takable, so with a plain mutex the LVGL task freezes on the first hub-opening tap.
No watchdog fires, because the producer tasks keep the ack and idle path alive.

``knob_ui.c`` therefore wraps every take and release in a holder task plus a depth counter.
A same-task take bumps the depth; an unlock from a non-owner logs an error and is ignored.
``main.c``'s ``lvgl_lock``/``lvgl_unlock`` delegate to it.
The clocks cannot hit this: their seam locks ``s_rgb_mtx``/``s_panel_mtx``, never the LVGL task's own mutex, and boot/wake ``loadScreen`` runs lock-free on other tasks.
The full synchronization census behind that claim (the wrapper's sixteen entries feeding its one physical take, the clocks' two keys, the six fences and zero queues) is :ref:`the guards sketch <guards-arch>`, counted in :ref:`the guard ladder <guard-ladder-perf>`.

The runtime switch and the per-second chain
...........................................

``KNOB_UI_ENABLE`` (the ``knob_ui`` Kconfig umbrella) gates the seam at runtime, not in CMake.
The seam objects always link (the early pass forbids conditional REQUIRES anyway), and ``main.c``'s ``#if`` decides whether ``moon_ui_init``/``ui_init`` run.
With the switch off, the legacy own-screen view comes up instead.

The family per-second chain rides the shared ``clock_rtc_interrupt_handler_cb`` on an ``lv_timer`` (1300 ms, the clocks' cadence).
It has exactly the clocks' three registrations (clock labels plus day probe, alarm display, alarm trigger) and no idle pair: the knob's ladder is ``knob_power``'s, not the app-layer dimming.

Two calls in ``main.c`` make the shared chain tick on this board:

- ``clock_minute_enable_interrupt(CLOCK_MINUTE_INT_FULL)`` after a successful ``clock_init``.
  On the software time base this is not a register write but the gate: ``clock_minute_process_interrupt`` reports a rollover only while the enabled type is FULL.
  Without it the ``lv_timer`` still synthesizes the countdown and the alarm fire (neither consults the gate), but ``clock_trigger_time_update`` never runs.
  The shared HH:MM then freezes at its last screen-switch value, visibly on the alarm screen while it rings.
  The boot log line ``Minute tick: software mode (no PCF85063A), type=2`` is the evidence the gate is open.
- ``action_ensure_ui_timers()`` in the single-threaded creation phase.
  On the clocks, ``action_wake_screen`` creates the shared 10 ms UI-tick and 1 s seconds-increment ``lv_timer`` timers, and the knob never runs that handler.
  The knob-guarded call creates or resumes exactly those two timers, with no wake side work; without them the seconds arc sits frozen between minute-tick writes.
  The boot evidence is the log line ``UI + seconds lv_timers up``.

With the minute gate open, the shared chain's hourly-sync leg runs as on the clocks.
At the top of a minute, on the airplane screen, with the radio down and NTP due, it runs the same background WiFi + NTP task.
The knob is usually joined, so the leg usually stays dormant.

The opening act
---------------

Every boot and every wake presents the static current phase: one crisp 360-px bake.

Deep sleep loses the PSRAM double buffer, so a wake re-bake has no old frame to keep on glass.
``KNOB_UI_LAST_FRAME`` (:commit:`43637e33`) covers that gap by persisting the last applied frame.
The producer writes the frame it just published to ``/spiffs/moon_frame.bin`` on the 512 K storage partition.
The file is one raw RGB565 square (259,200 B) plus a 24-byte header: magic, baked epoch, UTC day key, event signature, and a crc32 over payload, day and signature.
A torn power-loss write fails its crc and is not shown.
The producer writes only when the (day, signature) pair changes; a same-day late-NTP force-rebake skips the write.
The flash wear is therefore one ~259 K rewrite per UTC day or sky-event change, never one per wake.

The warm arm runs in ``app_main`` before ``ui_init``.
It stages the saved frame on the shared descriptor (``s_shown``) before ``create_screens`` attaches the images, so the images are born visible and the first screen flush already carries the moon.
``last_frame_warm`` skips its own flush when no images are attached, since that flush would only overpaint the retained moon.
This order matters on the knob alone: its LCD keeps the previous frame in panel RAM across the boot reset, while the AMOLED and P4 panels wake black.
The ``main`` task stack carries the 259 K SPIFFS read and the crc32 path, so ``CONFIG_ESP_MAIN_TASK_STACK_SIZE`` is 16384.

The warm arm shows a saved frame at most 2 days old (an older frame would show a visibly different phase).
It lands as one atomic whole-panel flush in the swap apply's shape: descriptor, unhide and ``lv_refr_now`` on the LVGL lock.
The caption is seeded from the saved instant.
(The shared home's gated row hides a plain time caption; the legacy own-screen view rolls it with the minute like any day-tick frame.)
The fresh bake then swaps over it as one atomic flush, with the seconds arc sweeping the re-bake's per-row watermark.

With no usable saved frame (the first boot after a fresh flash, which wipes the file with the spiffs image), the boot movie stages its frame 0 instead when ``KNOB_UI_MOVIE_ENABLE`` is on (:doc:`the boot movie <moon-movie>`).
The disc path (:doc:`the compute progress <compute-progress>`) is the floor under both.
There ``disc_publish`` bakes the quick phase disc (``knob_moon_bake_phase_dummy``, tens of ms) and publishes it through the ordinary ``s_pub``/swap machinery.
The swap unhides the disk with the disc on glass, and ``wait_ack()`` runs before the real bake reuses the other slot.
The fresh bake swaps over it old-frame-then-atomic, the arc sweeping its per-row watermark.
``maybe_persist_frame`` skips the disc, so ``/spiffs/moon_frame.bin`` always holds the last real bake.
Wake and day-tick re-bakes keep the old-frame-then-atomic posture.

The shared home carries one knob-only affordance through the render seam: a gated caption row attached to the producer's caption staging (``knob_ui_bind_caption``).
It is a montserrat-16, 240 px, DOT-centred label in the top band (+28; the middle belongs to the clock column, the foot to the battery icon).
Its probe geometry is ``x=60 y=28 w=240 h=18``.
The swap task reveals it only while the staged text is a failure state (``no time``, ``moon unavailable``); the shared clock row owns the daily text.
Every press is an ordinary home or hub touch.
The legacy own-screen build (``KNOB_UI_ENABLE=n``) keeps its own ungated bottom caption and screen handler.

The current-phase frame carries the sky event through the shared tint LUT (lookup table).
The 2025-03-14 total-lunar-eclipse epoch shows the blood tint on glass.
A 1 Hz day tick re-bakes exactly once when the UTC date or the event signature changes.
The ladder dims at idle 60 s after the presentation, with 0 WDT and 0 resets across 130 s.

Time: SNTP truth or nothing
---------------------------

The board has no RTC chip, so the display arms only on SNTP truth.
It uses the clock's own path: ``components/clock``'s software time base (the P4's posture, ``CONFIG_CLOCK_USE_PCF85063A=n``) with ``clock_sntp``'s one-shot lwIP POLL over the shared ``components/wifi`` join, which exists for nothing else.
An answer counts only with a plausible epoch (after 2021-01-01).
With no fix and no snapshot, the glass reads ``no time`` and no phase is ever baked from a guess.
This is the clock's never-a-phantom-alarm discipline applied to the moon.

Every trusted answer is also snapshotted to NVS, using the P4's pair (namespace ``clock``, key ``epoch``).
``clock_sntp`` saves it, and the power supervisor calls ``clock_nvs_save_epoch`` before every deep sleep.
A wake whose join and NTP window runs long still arms the static current phase at once from the snapshot plus uptime; the log says ``arming static phase on NVS snapshot``.
The late fix then re-bakes through the ``show_moon`` force-rebake path.
With ``KNOB_UI_LAST_FRAME`` the glass already shows the last frame baked during those seconds: it lands on the first arm, before the Wi-Fi + NTP window, and the snapshot only sets the instant the caption and the fresh bake are judged against.

The arm shows the sync window on the family's transient WiFi screen (:commit:`a9042ea2`).
It loads ``SCREEN_ID_WIFI`` right before the join wait.
The WiFi glyph there reads the connection state: grey while connecting, and ``tick_screen_wifi`` whitens it on connect (the knob's shared 10 ms ``ui_tick`` chain pumps it).
When the fix lands (in the window or late), the arm drops back to the home (airplane) screen.
The home↔WiFi switch is the both-moon plain atomic flush, so the saved frame stays on glass through it.

The NTP window is 20 s, so the worst case to a fix is about 24 s; the frame is on glass before that window starts.
A wake re-joins Wi-Fi in about 1.8 s and lands an NTP fix in about 9.8 s.
Join to fix measures about 4–11 s, agreeing with the host UTC clock within about 2 s.

A pre-seam wake that finds only the legacy ``knob_time``/``epoch`` snapshot adopts it once as last known and untrusted, and promotes it into the shared namespace.
The age of a snapshot cannot be verified offline.
That is why it is a fallback arm (with the plausibility floor enforced) and never a trusted instant.

While awake, the fixes are the boot fix, the late fix, and the shared chain's hourly on-demand refresh (``clock_sntp_needs_sync``, called from the shared ``actions.c``).
The UI reads the system clock once it is valid, so a correction applies without re-arming.
Bench builds freeze a test epoch (``KNOB_TIME_USE_TEST_EPOCH``) to keep the acceptance epochs reproducible.

The legacy own-screen view's caption (``KNOB_UI_ENABLE=n``) is a local wall clock ("Nov 8 17:29").
It is formatted through the POSIX zone in ``KNOB_TZ``, the same reading the clocks' home screen gives, so one moment shows one time on all three boards.
The 1 Hz day tick rolls it with the minute without a re-bake.
At the round panel's bottom edge the window is only about 205 px wide.
The label is therefore a fixed 240 px box with center align and DOT long mode, so an over-long caption becomes an ellipsis, never a clipped date.
The production (P1) build shows the shared home screen instead: the split-flap HH:MM pair cells, colon, alarm row and seconds arc from ``screens.c``'s ``create_user_widget_clock``, laid out at 360 px through ``UI_PX()``.
The caption path stays alive only in the legacy build.
``KNOB_TZ`` still sets that caption's zone, although the shared TIMEZONE screen it was meant to give way to has shipped (P2).

The power ladder
----------------

``components/knob_power`` runs a 500 ms supervisor task: active → backlight dim → ``esp_deep_sleep_start``.
It reads the same store pair the clocks' sleep screens own (P2).

- The dim rung fires after ``settings_idle_timeout_sec`` (the light-sleep screen's roller, ``settings.txt`` ``idle_timeout_sec``, default 60).
- The sleep rung fires ``settings_deep_sleep_countdown_sec`` after that (default 60).

This is the clocks' additive posture, both rungs measured from the last touch or dial activity.
The store is clamped to 60..1800 so it always answers; ``KNOB_DIM_AFTER_S`` (15) and ``KNOB_SLEEP_TIMEOUT_S`` (60) are the seed fallbacks behind it.

The sleep screens' power-off kill switches are read live on every pass:

- ``idle_timeout_enable`` off skips the dim rung (``dim_s = 0``), so the board stays at full brightness.
  A board already dimmed at the toggle returns to full on the next pass, because the un-dim branch treats ``dim_s = 0`` as "rung off", not "idle of 0 s".
  With the dim rung off, the deep rung fires after exactly ``deep_sleep_countdown_sec`` of idle.
- ``deep_sleep_countdown_enable`` off dims and stays dim: the ladder never calls ``esp_deep_sleep_start``.
  Real activity wakes it, and the wake paths are unchanged.
- Both off keeps the knob awake and undimmed.

The boot banner quotes each rung with "OFF" while its flag is off.

The dim rung has two parts, like the clocks' light sleep.
The supervisor drops the LEDC duty to ``KNOB_DIM_DUTY`` 64 = 25 % (the P1 dim-parity value that matches the clocks' light-sleep panel).
A registered dim observer (``knob_power_set_dim_cb``, which ``main.c`` wires to the seam's ``knob_render_set_home_dim``) drops the shared clock widget to ``UI_CLOCK_ALPHA_PCT`` (20 %) opacity alongside it.
Both are restored on activity.
The observer also hides the seconds arc while dimmed.
The arc is a sibling of the clock container in ``screens.c``, so the container's opacity never cascades to it.
The clocks pause the seconds timer at their light-sleep entry; the knob has no light-sleep screen, so the rung hides the arc, which also hides its ticking.

Activity is touch contacts (pushed from the input read) and dial detents on non-home screens only.
On home the dial is the brightness control (see `dial semantics <Dial semantics (P2)_>`_), so a detent there is not activity and never un-dims.
Only brightening from a dark panel reports wake intent, and the supervisor's un-dim then also restores the dimmed clock widget.
The boot arm and the opening act both hold the ladder, so a presentation is never interrupted.
The hold starts when ``boot_arm_task`` begins, not when the producer task exists.
That keeps a slow join + NTP window from dimming at 60 s and sleeping at 120 s over a glass that has not shown a frame yet.
Only a board that will never present (no assets, or no fix and no snapshot) leaves the ladder free to sleep it.
The ladder was device-verified on the earlier Kconfig pair: dim fired at idle 15.1 s and sleep at idle 60.1 s, with the USB-JTAG node vanishing host-side (a real deep sleep, not a hang).
The store-driven rungs are re-measured in the P2 bench pass.

The alarm across deep sleep
...........................

Before ``esp_deep_sleep_start`` the supervisor saves the shared ``clock``/``epoch`` NVS snapshot (``clock_nvs_save_epoch``), so the wake comes up on the last known instant.
Under ``KNOB_ALARM_TIMER_WAKE`` (default ``y``) it also arms the S3 internal RTC timer for the next fixed rule.
``knob_power_rule_epochs_around`` mirrors the scheduler's own ``mktime``/``tm_isdst=-1`` math over the loaded rule's hour, minute and weekday mask.
The sleep carries ``esp_sleep_enable_timer_wakeup(delta)`` next to the EXT0/EXT1 pads.

Three guards apply before sleep:

- A rule inside 60 s defers the sleep; the awake app-level esp_timer fires it, so the board never sleeps through its own alarm.
- A gap beyond ``KNOB_ALARM_MAX_SLEEP_GAP_S`` (82800, the Friday→Monday weekday hole) sleeps GPIO-only.
- An implausible epoch (before 2020) arms no phantom timer at all.

After the wake, ``main.c``'s wake arbitration decides what a TIMER wake means (the wake-cause legend on the device page spells it 4):

- Within ``KNOB_ALARM_GRACE_S`` (300) of the rule, recomputed from the snapshot or SNTP, the wake is the alarm.
  It shows the silent alarm visual through ``action_alarm_triggered`` under the LVGL lock.
- An early wake within ``KNOB_ALARM_LATE_MAX_S`` (900) holds dark at 1 Hz and fires at the rule, never early.
  It re-reads the clock each second, so a landing SNTP fix re-anchors the wait.
- Anything further off, or an implausible epoch, is a stale fire and becomes a normal dial-style wake.

RC-clock drift is the accepted cost (bench tolerance <= ~0.5 %).
The grace window absorbs it, and SNTP re-corrects it on every wake, so it never compounds.

Wake sources
............

Wake is EXT0 on dial-A GPIO 8, active-LOW against the board's own 10K pull-up (the S3 RTCIO accepts it, as the plan required).
The EC1 encoder pulses one channel per detent, so a detent that pulses only B (GPIO 7) does not wake.
The next detent in either direction pulses A and does, so the device never looks dead.

``KNOB_TOUCH_WAKE`` (default ``y``) also arms EXT1 ANY_LOW on the CST816S INT line (GPIO 9), so a screen tap wakes the panel like a detent.
One touch status read before sleeping clears a pending INT pulse; a stale low would re-wake the boot (the boot-loop guard).
A rejected EXT1 pad degrades to dial-only with a warning.
The back-side power button is a hardware rail switch, not a firmware input.

``KNOB_SLEEP_ALLOW=n`` is the sanctioned log-only bench mode.
The ladder runs unchanged up to the sleep call, and then the board settles into a stable dark state (backlight 0, ladder frozen).
It looks like a slept board, except that USB-JTAG stays up.
Real activity (a touch, or brightening the home dial) wakes it; the dial's wake intent is the software stand-in for the EXT0 dial wake.

The ladder and the alarm, as one sketch
........................................

The left chain is the ladder (awake → dim → sleep entry).
Each rung is live-gated by the sleep screens' power-off kill switches, drawn as the two dashed skip branches.
The branch under the entry is the RTC TIMER's three guards, and the right fan-out is ``main.c``'s wake arbitration for a TIMER fire.
The arrow to read twice is the rule that an early wake holds rather than rings.

.. raw:: html

   <pre class="mermaid">
   flowchart TD
     ACT["awake — 500 ms supervisor; activity = touch, dial detents off-home only"]
     ACT -->|"idle after settings_idle_timeout_sec (60) — rung armed"| DIM["dim rung — LEDC duty 25 %, clock alpha 20 %, seconds arc hidden"]
     DIM -->|"touch / brightening"| ACT
     DIM -->|"countdown settings_deep_sleep_countdown_sec (60) after that — rung armed"| ENTRY["sleep entry — save the clock/epoch NVS snapshot"]
     ACT -.->|"kill switch off (idle_timeout_enable): dim rung skipped — sleep at idle = countdown"| ENTRY
     DIM -.->|"kill switch off (deep_sleep_countdown_enable): stays dim, never esp_deep_sleep"| DIM
     ENTRY --> CALC["KNOB_ALARM_TIMER_WAKE: knob_power_rule_epochs_around (mktime, tm_isdst = -1, rule hour/minute + weekday mask)"]
     CALC -->|"rule inside 60 s"| DEFER["sleep DEFERRED — the awake esp_timer fires the alarm"]
     CALC -->|"gap beyond KNOB_ALARM_MAX_SLEEP_GAP_S (82800)"| GPIO["GPIO-only sleep — no RTC TIMER"]
     CALC -->|"epoch implausible (before 2020)"| NOARM["no phantom timer armed"]
     CALC -->|"otherwise: delta to the next rule"| ARM["RTC TIMER armed — esp_sleep_enable_timer_wakeup(delta)"]
     NOARM --> SLEEP
     GPIO --> SLEEP["esp_deep_sleep_start — EXT0 dial-A GPIO 8 + EXT1 touch GPIO 9 always armed"]
     ARM --> SLEEP
     SLEEP -->|"EXT0 / EXT1"| NORM["normal dial-style wake"]
     SLEEP -->|"RTC TIMER (wake-cause legend 4)"| ARB{"wake arbitration — rule recomputed from the snapshot / SNTP"}
     ARB -->|"within KNOB_ALARM_GRACE_S (300) of the rule"| FIRE["it IS the alarm — action_alarm_triggered (silent visual)"]
     ARB -->|"early, within KNOB_ALARM_LATE_MAX_S (900)"| HOLD["holds DARK at 1 Hz — a landing SNTP fix re-anchors — fires AT the rule, never early"]
     ARB -->|"further off, or implausible epoch"| STALE["stale fire — normal dial-style wake"]
   </pre>

.. _dial-semantics-knob:

Dial semantics (P2)
-------------------

The dial is an input device, not a second UI: it steps one per-screen edit cursor, and a tap commits it.
Touch stays fully native everywhere, so the dial is additive.
The EC1 has no push-button, so the tap is the only commit gesture.
The whole input chain (encoder pulse pair, the 20 ms consumer task, the polled CST816S, and where the dial task meets the LVGL lock) is drawn in :ref:`input-chain-ui <input-chain-ui>`.

Two files carry it.
``components/knob_ui/knob_dial.c`` runs the input side.
A 20 ms task consumes the debounced encoder count (vendor pulse-pair scheme, A-rise +1 / B-rise −1) one detent at a time under the LVGL mutex.
Every rotation is ladder activity, except the home brightness turn in the table below.

Tap-to-commit rides the delivery path of the home tap → hub gesture.
A background CLICKED lands on the screen root, where a commit listener runs inside the LVGL task's lock.
The settings creators deliberately clear the root's CLICKABLE flag, and in LVGL 9 a press with no clickable object under the finger has no target at all, so no CLICKED fires anywhere.
``knob_dial_start`` therefore re-adds CLICKABLE to every settings root, knob-side only.
That is harmless: the screens are exactly panel-sized, so nothing can scroll, and the clocks never see the line.
(The home keeps its root clickable; its tap also needed the reentrant LVGL mutex described in `why the LVGL mutex is reentrant`_.)

The screen side lives in ``components/ui/screens.c`` behind ``#ifdef CONFIG_BOARD_KNOB`` (the trio ``screens_dial_init`` / ``screens_dial_step`` / ``screens_dial_commit``).
Only the knob's board-shim ``choice BOARD`` defines that symbol, so in the clocks' builds the preprocessor drops the block and their UI is structurally untouched.
Even in the knob's own build the block adds no objects: between ``screens.c:4185`` and its ``#endif`` at :4607 there is not one ``lv_*_create``.
The dial cursor only restyles the existing shared population (the 255-object build-once city it restyles is counted in :ref:`the assembly ladder <assembly-ladder-perf>`).

The commit never duplicates handler logic: it re-sends the event the touch path already handles.

- Icons, the alarm glyphs, exit buttons, WiFi rows and the knob's WiFi roller receive a synthetic ``LV_EVENT_CLICKED``, the same event the finger produces.
- Rollers and the weekday-arc range handles receive ``LV_EVENT_RELEASED``.
  That is their commit (store save, reschedule, alarm-rule save), identical to lifting a finger off a drag.
- The wheel letter goes through ``settings_wheel_add_slot``, the ring's own commit choke point.

A guard makes a stray background tap inert: nothing commits unless a cursor moved since the screen was entered.

.. list-table::
   :header-rows: 1
   :widths: 26 42 32

   * - Screen
     - Dial turn
     - Tap (background)
   * - Home (modem-sleep screen)
     - Brightness control (:commit:`47c5e8b0`).
       CW brightens, CCW dims, 5 % per detent through the seam's ``moon_ui_panel_brightness_set``.
       It is floored at the ladder's own dim rung (``KNOB_DIM_DUTY`` = 25 %), so the dial never goes darker than the ladder's choice.
       A turn is not ladder activity, so fiddling never un-dims; only brightening from a dark panel reports wake intent.
     - open the hub (the native home-tap handler)
   * - Other moon screens
     - activity, un-dims the ladder (no cursor)
     - open the hub (the native home-tap handler)
   * - Hub ring
     - step focus over the icons and the central exit (white glyph = focused, ring on the exit)
     - open the focused icon / exit to home
   * - Alarm screen
     - step the three glyphs (master toggle, pomodoro, alarm clock) with a focus ring
     - toggle / open through the native handlers
   * - Roller screens (pomodoro h/m, alarm time h/m, both sleep
       screens, timezone, alarm sound)
     - Step the focused roller (wraps, animated); pressing the other roller of a twin screen re-focuses it.
       On the two sleep screens with the timer kill switch off, the dial layer refuses the step: the roller is locked, and ``lv_roller_set_selected`` bypasses the LVGL disabled state, so the refusal cannot ride the disabled flag.
     - ``RELEASED`` = commit (store save / reschedule).
       On a locked sleep roller the commit is a natural no-op (the cursor never moved), and the handler's own guard refuses it anyway.
   * - Weekday arc (alarm-time screen, P3)
     - step the focused range handle across the seven day cells; touch-pressing the other handle re-focuses it (touch-dragging the arc keeps working alongside)
     - ``RELEASED`` on the focused handle = save (the native handler writes the rule and re-arms the scheduler)
   * - WiFi list (an ``lv_roller`` on all three boards, :commit:`47c5e8b0`, :commit:`368d49aa`)
     - Roll the list; the window's centre is the cursor.
       Entries use the tier's 48-baseline SSID font (the ``_48_ssid`` file: 37 px knob / 48 px S3 / 74 px P4).
       They are uppercased, with the SSID truncated to 14 chars + ``+`` (the raw name still reaches the keyboard) and the ``NNDBM`` suffix.
       The tails clip at the panel's chord; the SSID head always reads.
       NORMAL mode clamps at the ends like a real roller.
     - open the keyboard / connect (the roller's own ``CLICKED``, which the dial commit re-sends)
   * - Wheel keyboard
     - step the hot slot; the sector reveal follows (the sticky reveal a touch press does)
     - commit the hot letter into the running entry
   * - Boot arm / boot bake
     - activity (the ladder holds; a turn never interrupts the act)
     - an ordinary home/hub touch

The wheel keyboard needs one knob-only z-order change.
At knob scale the letters render at r≈130 px.
The mode register arc is created after the character catch, so it sits above it in z-order.
It answered presses up to r≈121 and swallowed the natural ring press before the catch saw it (zero ``entered letter`` lines; presses logged as ``settings_wheel: mode``).
The knob's build therefore lifts the catch above the register arc (``lv_obj_move_to_index``, a ``CONFIG_BOARD_KNOB`` branch in ``screens.c``).
Presses at r ∈ [108, 180] commit letters through the catch's native annulus.
Presses inside it still reach the register arc's four 60° mode keys, and COMMIT/DISCARD (created later) stay above both.
The clocks never compile the lift.

Battery gauge (P4)
------------------

The gauge is the knob's only addition to the home screen.
It is a knob-side overlay by design, so the shared home stays byte-identical.

.. graphviz:: ../_graphs/knob-battery.dot
   :alt: The knob battery read path, from ADC1 channel 0 behind a 10K/10K divider to a percent and a charging flag.
   :caption: The read path. One ADC read on ``ADC1_CH0`` (ADC2 is unusable with the radio on) at 12 dB; an init failure turns the gauge off and makes every entry point inert. The eFuse calibration falls back to raw × 3100/4095 (placeholder accuracy either way), then the 10K/10K divider. The two amber boxes are placeholders waiting on the multimeter ritual: the 3300–4200 mV percent pair and the ≥ 4400 mV docked-USB charging heuristic (bench ~4522 mV). The hardware side is :doc:`the knob wiring <../devices/knob>`.
   :align: center

.. graphviz:: ../_graphs/knob-battery-ui.dot
   :alt: The battery icon on the home screen and its four display states, refreshed once a minute.
   :caption: The view. A capsule and nub at ``BOTTOM_MID`` minus ``UI_PX(26)``, added last and click-transparent so the home tap → hub gesture still owns every tap. A 60 s timer refreshes it after an immediate first fill. The four states: dead-chrome grey capsule, the hand-rasterised bolt over the theme's moon color while charging, red at or below ``KNOB_BATTERY_LOW_PCT``, and proportional white otherwise.
   :align: center

``knob_battery_ui.c`` adds a status-bar battery icon (in the iPhone style) to the shared home root after everything else.
It is a grey-outlined capsule with a right nub and a proportional fill, and it is click-transparent so the home tap → hub gesture keeps owning every tap.
It sits bottom-centre (``BOTTOM_MID`` with a ``UI_PX(26)`` foot lift at the 360 panel, the single calibration constant), and a one-minute ``lv_timer`` refreshes it.
The cell moves on the minute scale, not the second.

- The fill is white, and red at or below ``KNOB_BATTERY_LOW_PCT``.
- While ``knob_battery_charging()`` is true, a hand-rasterised bolt sits over a fill in the theme's moon color (``theme_colors[active_theme_index][0]``, the seconds-arc ink, :commit:`3cd836aa`).
  It is read live, so a theme change shows on the next minute tick.
- The charging state is a heuristic, not a signal: a cell read at or above ``KNOB_BATTERY_CHARGING_MV`` (0 = bolt off) is the docked-USB signature (bench ~4522 mV, far over the cell ceiling).
- With no reading, the capsule is empty and pale grey.

The icon shows no digit label (:commit:`e9fbd623`).

The icon overlaps the moon rather than making room for it.
The shared home bakes the disk centred and contained: r 150 at cy 180, with the lunar limb 12 px inside the seconds-arc ink.
(get_center's radius ``(360 − 2*UI_PX(10))/2 = 173`` is the stroke's outer edge, and the theme's 12 px stroke is inked inward from it, so the ink spans 162..173.
The clocks keep limb-to-ink gaps of 12 px (S3) and 9 px (P4).)
The render seam sets the geometry through ``knob_ui_set_disk_geom`` (:commit:`28162660`), so the moon sits inside the seconds arc as on the P4 and the S3 AMOLED.
The legacy own-screen view keeps the foot-band bake (r 150 at cy 168), whose 12 px below centre keeps its bottom-edge caption row clear of the lunar limb (:commit:`19351f01`).

The reading lives in ``components/knob_battery``.
It uses ``adc_oneshot`` on ADC1 channel ``KNOB_BATTERY_ADC_CHANNEL`` (GPIO 1; ADC2 is unusable with the radio on) at 12 dB attenuation.
The eFuse curve-fitting calibration converts the raw value, with a linear raw approximation as fallback.
The result is scaled by ``KNOB_BATTERY_DIV_NUM``/``_DEN`` across the 10K/10K divider, then mapped linearly between ``KNOB_BATTERY_MV_EMPTY`` and ``KNOB_BATTERY_MV_FULL`` (clamped).
``knob_battery_init`` and ``knob_battery_ui_attach`` run in ``main.c`` inside the P1 umbrella.
With ``CONFIG_KNOB_BATTERY_GAUGE=n`` or a failed ADC init, every entry point is inert, the icon never appears, and the log says ``gauge off``.
The first read logs ``raw -> mV sense -> mV cell``, the bench line the multimeter ritual will check against.

.. _knob-crash-witness:

The field-crash witness (gasp + coredump)
-----------------------------------------

The knob carries its own black box, armed in every production build.
It exists because a panic reset drains the USB-CDC TX FIFO before any backtrace flushes, so a field crash leaves nothing on the serial log.
The witness has three parts and one bench repro.

The gasp marker
...............

The gasp marker is 24 bytes of ``RTC_NOINIT_ATTR`` memory (``s_gasp`` in ``knob_ui.c``).
Single atomic word and byte stores stamp it on the producer, swap, seam and flush hot paths:

- a stage byte (BAKE / WAIT_ACK / PUBLISH / SWAP_LOCK / SWAP_REFR / SWAP_DONE / SWITCH\_\* / FLUSH / FLUSH\_DONE);
- the pub and applied frame counters;
- the LEDC duty and a fade-pending mirror.

RTC slow memory survives every reset except power-on.
The boot after a crash prints one line, ``gasp: crash-reboot reason=… pub=… applied=… stage=… duty=… fading=…``, naming the section the board was in.
The write is magic-gated (``start_tasks`` arms it, the report clears it).
The report is gated on crash reset reasons (panic, int-WDT, task-WDT, WDT, brownout), so power-on and dial/tap deep-sleep wakes stay quiet.

The prime suspect the stages exist to tell apart is a lost GPSPI completion.
It would park ``esp_lcd_panel_draw_bitmap`` on ``portMAX_DELAY`` forever, because the stock ``esp_driver_spi`` has no watchdog like the clocks' ``panel_wdg``.
The swap would then die at SWAP\_REFR/FLUSH with ``pub != applied``.

The coredump ring
.................

A 64 K ``coredump`` partition sits at the tail of ``partitions_knob.csv``, with ``CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`` (ELF format).
A panic writes an ELF core there that outlives the reset.
On the churn axis its schedule is a flat zero: it is the only media target whose every write is an accident, not a timer (:ref:`the write ladder <write-ladder-perf>`).

Read it back on the bench:

.. code-block:: console

   esptool.py --port <port> read_flash 0x710000 0x10000 core.bin
   espcoredump.py info_corefile -t raw -c core.bin build/knob/moon-phase.elf

Pass the whole partition file, format ``raw``.
Stripping a 24-byte flash header and passing ``-t elf`` StreamErrors partway through the ELF on IDF 5.5.1.
The ELF is the knob build's own (``build/knob/moon-phase.elf``, gitignored like the clock's).
This IDF tree registers no ``idf.py coredump-read`` action.
The new partition table must be flashed once; the added partition sits after ``moon_assets``, so nvs, SPIFFS and assets stay intact.

The witness self-test
.....................

``KNOB_DIAG_PANIC_AT_FRAME`` aborts the producer after N published frames.
It rides the static boot bake, where N = 1 is the meaningful value.
The bench proof ran at frame 320:

- the crash printed ``abort() was called at PC 0x4201923c on core 1``;
- the boot after printed ``gasp: crash-reboot reason=4 pub=320 applied=319 stage=0 duty=255 fading=0``, exactly the crash-time state (320 published, 319 applied, producer just past publish);
- the flash dump decoded cleanly: ``Found core dump 24324 bytes``, checksum correct, crashed task ``knob_moon``, PC in ``panic_abort``, the abort PC resolving to the hook.

The one remaining bench-only hook (``KNOB_DIAG_FRAME_TRACE`` boundary logs) stays in the Kconfig with production default off.

The crash the witness caught
............................

A field report said the knob crashed while the user adjusted the Settings **display** roller, and this time it left a coredump.
Decoded with the recipe above, it was an IllegalInstruction inside the SYSTICK tick ISR on IDLE0.
``xTaskIncrementTick`` → ``prvTaskExitCriticalSafeSMPOnly`` returned to a garbage PC (``0x75805c38``, with ``a7 = 0xcdcd``, the stack-poison signature), and the dump said "Crash happened in the interrupt context".
At that moment ``knob_moon`` sat deep in erfa: ``moon_task`` → ``static_once_body`` → ``knob_moon_display_begin`` → ``moon_event_classify`` → ``full_moments`` → ``se_argmax`` → ``se_dot`` → ``moon_geometry`` → ``eraEpv00``.

``knob_moon`` was the only task with a PSRAM-backed stack.
With ``CONFIG_FREERTOS_SMP`` unset (the original kernel), every critical section snoops the other core over IPC.
So the core-0 tick walks that PSRAM stack while the cache underneath it is the thing in question.
The adjustment triggers it: the roller's commit is a ``settings_store_save()``, a spiffs write, and a spiffs write drops both caches, the exact condition IDF's assert names.
The task table in the dump ruled out a stack overflow (``knob_moon`` had touched 2624 B of its 12288).
The re-push hypothesis was falsified in code (``ori_apply_v10`` NULL-guards all 17 roots).
This is the ``b924abf`` systimer-tick family, with its cause finally known: a PSRAM-stack task must not be the running task on the side of a flash write.
The fix is the internal-RAM stack described in `the frame path`_: 12 KiB taken from the 194,817 B of DIRAM the build reports free (its DIRAM row: 146,943 B used of 341,760, 43 %).

The repro that proved it
........................

``KNOB_TEST_CLASSIFY_STRESS`` (bench Kconfig, production ``0`` = dead) replays the chain at boot.
It runs N passes of ``knob_moon_classify_signature()`` over a swept epoch range, each preceded by a ``settings_store_save()``.
Building it showed two things:

- erfa alone never fails (400 passes clean), so the crash needs the flash write, not only a hot core 1;
- a loop without a ``vTaskDelay(1)`` per pass starves the core-1 idle and earns a naked ``TG1WDT_SYS_RST`` instead of the bug under test.

Pinned to 400, the PSRAM-stack build asserted inside a single pass: ``assert failed: spi_flash_disable_interrupts_caches_and_other_cpu cache_utils.c:127 (esp_task_stack_is_sane_cache_disabled())``.
The internal-RAM build ran 200+ passes with no assert.
That A/B is the regression test: re-pin the key, flip the stack cap back to ``MALLOC_CAP_SPIRAM``, and watch it fail.

With the witness shipped, the next field crash is answerable without a repro.
Read the gasp line from the boot log after the crash and, if the panic path completed, the coredump for the full backtrace and task list.

Not shipped (on purpose)
------------------------

- The battery calibration.
  The gauge code shipped with P4; verified numbers did not.
  The divider pair and the 3300..4200 mV LiPo pair are the plan's sanctioned placeholder until the multimeter ritual, with a bench operator present, settles whether the divider's top rail is the cell or VBUS.
  The schematic draws it as 5V and the factory gauge reads an impossible 4.6 V.
  Until the measurement agrees within about 50 mV, the gauge reads as plausible, not verified.
  The verdict, either way, belongs on the device page.
- The deep-sleep alarm round-trip bench pass.
  Editing is complete (P3): the alarm shows (always silent, with no codec to ring), and the hour/minute rollers, the weekday-arc range handles, the master toggle and the pomodoro entry all work by dial and tap.
  They save through the native handlers into ``/spiffs/alarm.txt`` and re-arm the scheduler.
  What remains is the sleep-on session: the short-countdown TIMER fire on a deep-slept board (the ladder runs log-only for now, per the bench rule).
- Orbit ambient tasks, sub-hour TZ granularity, and real charge detection (there is no known VBUS sense; the bolt is the docked-USB heuristic above).

Credentials are entered on the glass (P2): WiFi roller → wheel keyboard, where COMMIT saves and connects through the native handler that writes the gitignored credentials file.
Host-side editing of that file stays available.

Where to go next
----------------

- :doc:`the compute progress <compute-progress>`, for the phase disc and the progress arc the knob shares.
- :doc:`the boot movie <moon-movie>`, for the knob's movie loop between the warm arm and the first bake.
- :doc:`the Knob board <../devices/knob>`, for the wiring, pins and wake-cause legend.
