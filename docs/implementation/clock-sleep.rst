The clock and the sleep machine
===============================

.. brief:
   Reader: a firmware developer working on the UI, the clock or power states.
   They already know: C, FreeRTOS tasks, LVGL basics, the atomic-frame rule.
   After this page they can: say which screen is up when, what moves it, and which code path each sleep entry and wake takes.
   Page type: explanation.

The moon pipelines decide what the screen shows.
This page covers the machine that decides *when* it changes: the minute-driven clock, the 90° orientation steps, the idle and sleep states, and the three ways to wake the device (touch, tilt, alarm).
It is the part of the product where a clock and a moon have to agree on every minute of the day.

The short version
-----------------

The page answers one question: when does the screen change, and who may change it?
Four answers cover everything below.

1. **Two moon screens, one image.**
   The home screen (internally "airplane") and the WiFi screen carry the same moon picture.
   The WiFi screen adds a sync indicator, shown only while a time sync runs (boot, hourly, manual).
   Because the images match, switching between the two needs no fade.
   The settings hub and its sub-screens show no moon at all.
2. **Nothing moves until something changes.**
   The clock updates only when the displayed minute rolls, and never while the panel is dark.
   Each digit sits in its own fixed-width box, so the string never shifts sideways.
   The screen snaps between four 90° poses instead of following the tilt.
   Every visible change lands as one atomic flush, like every other frame in this product.
3. **Two sleep rungs, two timers.**
   After a minute with no touch and no sustained tilt (the default, user-tunable), the device enters *light sleep*.
   The moon freezes, the panel dims to 25 % and the clock drops to 20 % opacity; the dim is the sleep indicator.
   A one-shot countdown, armed at that moment, later fires *deep sleep*.
   Deep sleep hides the clock, shuts down the WiFi radio and turns the backlight off.
   The RTC crystal keeps counting, which is what lets the alarm find its minute.
   The knob is the one board that really powers down.
4. **Three doors, one handler.**
   Touch, tilt and alarm all run the same wake tail.
   It stops any ring, relights the panel and shows the moon at its current phase at once.
   The network time check runs on a background task, so the freshly woken screen never waits for the radio.

How the states connect
----------------------

Read the diagram as the page in one picture.
The left side is the awake life: boot, sync, home, and the settings hub on a tap.
The right side is the sleep ladder: home, dim, dark.
The arrows that climb back up are the three wake doors.
Each labelled transition (the 60 s idle timeout, the 60 s countdown ``UI_DEEP_SLEEP_COUNTDOWN_SEC`` with a minimum of 60, the two wake routes, and the P4 staying dim while an alarm is pending) has its own section below.

.. raw:: html

   <pre class="mermaid">
   stateDiagram-v2
     [*] --> BOOT
     BOOT --> WIFI: NTP ok, static-phase bake
     BOOT --> AIRPLANE: NTP fail
     WIFI --> AIRPLANE: sync complete
     AIRPLANE --> HUB: tap, the sub-screens (exit returns with no sync kicked off)
     HUB --> AIRPLANE
     AIRPLANE --> LIGHT_SLEEP: idle 60 s, static dim (25% panel, 20% clock alpha, countdown armed at entry)
     LIGHT_SLEEP --> AIRPLANE: wake (touch / tilt / alarm), backlight to 100, static phase, on-demand sync
     LIGHT_SLEEP --> DEEP_SLEEP: RTC countdown (60 s default)
     DEEP_SLEEP --> LIGHT_SLEEP: ALM pending (P4 stays dim, does not drop out)
     DEEP_SLEEP --> AIRPLANE: wake, ALM pin (S3) / software one-shot (P4), or tilt
     DEEP_SLEEP --> DEEP_SLEEP: touch, none on the S3
   </pre>

Three rules hold on every edge.
Every transition is one atomic flush.
Every panel brightness write goes through the serialized 0x51 path (:doc:`the display pipeline <display>`).
Every animation is display-paced off a once-built map (:doc:`the animation pipelines <animations>`).

Vocabulary
----------

Sixteen terms carry the page.
The ones shared with the rest of the firmware also have fuller entries in the :doc:`glossary <../glossary>`.

Tick and LVGL task
   The UI worker.
   Its tick timer runs every 10 ms awake (100 ms in light sleep); the moon tick inside it runs every 50 ms.
   It redraws what changed and pushes the finished frame to the panel.
   Every change you see rides on this task.
Activity timer and idle timeout
   The countdown that a touch or a sustained tilt resets.
   When it expires (``settings_idle_timeout_sec``, default 60 s, range 60–1800) the device steps down the sleep ladder.
RTC
   Real-time clock: a small chip with its own crystal (the PCF85063A on the S3).
   It keeps counting seconds while the main chip idles, and the alarm and the countdown ride on it.
ALM pin
   The RTC chip's alarm output.
   When the armed alarm instant arrives, the pin toggles and wakes the device to ring.
One-shot
   A countdown programmed to fire once after a chosen delay.
   The deep-sleep countdown is a one-shot armed the moment the device goes quiet, so sleep depth never depends on an animation.
App-level deep sleep
   On the clocks, deep sleep is a software state, not a power-down.
   The backlight is off, the timers are paused and the radio is shut down, but the chip keeps running.
   That is why the P4 can stay dim when an alarm is pending.
   The knob's deep sleep is a real power-down (``esp_deep_sleep_start``).
Airplane (modem-sleep) screen
   The home screen's internal name.
   While it is up the WiFi modem is off; the firmware turns the network on briefly, on demand, to check the time.
Alpha
   Opacity in percent.
   The clock at 20 % alpha is drawn one-fifth as opaque, so the AMOLED's black shows through the digits.
0x51 brightness ramp
   The panel's own dim and brighten command (glossary: *fade-to-black*).
   It changes the brightness of every pixel at once and writes no pixels, so it fits inside the atomic-frame rule.
EMA (exponentially weighted moving average)
   A slowly following average.
   Each new sample nudges the running value a little, so a one-off jolt barely moves it and a held pose gradually becomes the settled pose.
int4 quantization
   Rounding each gravity axis to a coarse integer grid where one level is 1/8 g (clamped to ±1 g).
   Table jitter never counts as motion, and everything downstream is integer arithmetic.
Dot and cross
   The two integer products that answer the tilt geometry without trigonometry.
   The dot says how strongly the sampled direction agrees with the home axis; the cross says which side of it the sample falls on.
   Together they pick the quadrant.
Departure wake
   Waking on a sustained move away from the settled pose, not on raw motion (see the glossary).
   It uses two per-state thresholds (the 5° activity bar awake, the 20° wake bar asleep), held for two consecutive samples.
SNTP and on-demand sync
   The network time check the device runs at boot, hourly, and on wake when the RTC has drifted enough to distrust.
   The radio runs only for the duration of the check.
LEDC duty
   The knob's dimmer: an ESP32 PWM channel whose duty cycle sets the backlight.
   It is an 8-bit scale; duty 64 matches the clocks' 25 % panel rung.
EXT0 and EXT1 pads, NVS snapshot
   EXT0 and EXT1 are the physical wake inputs of a chip that is really asleep (turning the knob, tapping it).
   The NVS snapshot is the rough current time the knob stores in flash before sleeping.
   It tells the knob approximately when it woke, until the network corrects it.

The screen model
----------------

Two screens in the product carry the moon, and they carry the same image.
The firmware always knows which family the visible screen belongs to (``moon_screen_has_image()`` feeding ``s_moon_screen``).
That knowledge is what makes the switch between the two moon screens free: same picture, nothing to hide.

The home screen is the **airplane (modem-sleep) screen**.
It shows the moon in the foreground, the clock label and the seconds arc, with no icons.
The **WiFi screen** is the same moon image plus a transient indicator.
It is up only while a sync runs (boot, hourly, manual), then the device drops back to airplane.
Because both screens carry the same moon image, the switch is the both-moon case of the fade rule (:doc:`the animation pipelines <animations>`): no dim, one atomic whole-panel flush.

.. figure:: ../img/screens/modem_sleep.png
   :alt: The airplane home screen with the moon, the clock and the seconds arc.
   :figclass: align-center

   The **airplane** (modem-sleep) screen: the moon, the minute and the seconds arc, with no icons.

.. figure:: ../img/screens/wifi.png
   :alt: The WiFi screen: the same moon with a sync indicator.
   :figclass: align-center

   The **WiFi** screen: the same moon image plus the sync indicator, shown only while a sync runs.
   The switch back to airplane is the no-dim both-moon case.

The second board, the Waveshare ESP32-P4-WIFI6-Touch-LCD-XC (720×720 round IPS, :doc:`the P4 4C port <../devices/p4>`), runs the same UI.
Its moon bakes at a larger disk (650 px) and its clock digits use the 148-px font.
The screen logic (these transitions, the wake paths, the sleep states) is the machine this page describes, unchanged.

.. figure:: ../img/screens/p4/modem_sleep.png
   :alt: The airplane screen on the 720-pixel P4 board.
   :figclass: align-center

   The **airplane** screen on the P4 board (Waveshare ESP32-P4-WIFI6-Touch-LCD-XC).
   The layout is the same at 720×720: the moon at 650 px, the 148-px clock digits, the seconds arc.

A tap on the home or WiFi screen opens the **settings hub**.
The hub and its sub-screens (alarm, pomodoro, alarm time, light sleep, deep sleep, timezone, WiFi) carry no moon image.
``moon_screen_has_image()`` compares the active screen's root with the moon images' roots, and the result feeds ``s_moon_screen``, the moon-screen display gate.
The gate tracks which screen carries the image; the hourly refresh probe reads it.

.. figure:: ../img/screens/settings_hub.png
   :alt: The settings hub with its sub-screen icons around the exit button.
   :figclass: align-center

   The **settings hub**: the sub-screens arranged hub-and-spoke around the exit.
   It shows no moon, so the display gate counts it as a moon-less screen.

The sub-screens:

.. figure:: ../img/screens/settings_alarm.png
   :alt: The alarm rules screen with one example rule.
   :figclass: align-center

   The **alarm** hub icon opens the rule list (add, delete, edit).
   The project default ``alarm.txt`` holds only comments ("# no alarms"), so a fresh flash never arms a ring nobody asked for.
   The screenshot shows the host simulator's canned example rule.

.. figure:: ../img/screens/settings_alarm_time.png
   :alt: The alarm time screen with a week arc and a time wheel.
   :figclass: align-center

   The **alarm time** screen.
   The week arc (Mo–Fr here) selects the rule's active days; the time wheel picks the hour and minute.

.. figure:: ../img/screens/settings_pomodoro.png
   :alt: The pomodoro repeat-interval screen.
   :figclass: align-center

   The **pomodoro** interval: the focus timer's repeat interval in minutes.

.. figure:: ../img/screens/settings_deep_sleep.png
   :alt: The deep-sleep countdown settings screen.
   :figclass: align-center

   The **sleep countdown**: the seconds from light sleep to deep sleep.
   The one-shot is armed at the light-sleep entry itself.

.. figure:: ../img/screens/settings_tz.png
   :alt: The timezone settings screen showing Europe/Berlin.
   :figclass: align-center

   The **timezone** screen: one of the 67 curated named zones (Europe/Berlin here).
   Committing rewrites the RTC epoch-consistently, re-arms the alarm and re-gathers the moon for the zone's city (:doc:`the architecture <architecture>`, ``clock_tz``).

.. figure:: ../img/screens/settings_wifi.png
   :alt: The WiFi networks screen listing scan results.
   :figclass: align-center

   The **WiFi networks** screen: the scan results from the quiescent AP scan of the on-demand sync (:doc:`the architecture <architecture>`).
   Selecting a network opens the password entry with the on-screen keyboard (:doc:`the round keyboard <../keyboard>`).

The minute-driven clock
-----------------------

The home clock is not animated.
It changes when the displayed minute rolls, and the update runs on the LVGL task.
While the panel is dark the update is skipped; the RTC keeps time and the first wake re-applies it.
The digits never slide sideways, because each character lives in its own fixed-width box.

``action_update_clock_display`` fires when the displayed minute changes.
It writes the clock label and refreshes the seconds arc.
A 1.3 s poll notices the rollover (``clock_interrupt.c:26``); how late the update can land is drawn in :ref:`the freshness ladder <freshness-ladder-perf>`.

The backlight gate skips the update while the panel is off in deep sleep (``g_backlight_off``).
The RTC crystal runs through the app-level deep sleep, so time stays correct.
The next wake restores the panel, and the minute update applies again.
Nothing else gates the on-demand WiFi sync except the policy in `The hourly sync`_.

The minute label is a row of **fixed-width HH:MM slots**.
The numbers font is proportional: as one label, ``11:11`` would be 175 px wide and ``44:44`` 290 px, and the string would shift on every minute change.
Each character sits in its own centred, fixed-width slot label instead.
The string keeps a constant width and only the ink moves.

.. figure:: ../img/screens/settings_time.png
   :alt: The set time and date screen with fixed-width clock digits over the date.
   :figclass: align-center

   The **set time/date** screen.
   The clock digits use the same fixed-width HH:MM slots as the home screen, over the date; the X commits the time.

The wall clock's chain of trust
-------------------------------

Every minute the clock shows and every phase the moon bakes traces back to one of **three time bases**.
The S3 has its PCF85063A, the P4 its system clock over the internal RTC timer, and the knob an NVS snapshot.
One flag (``s_time_trusted``, ``clock_sntp.c``) decides how far any of them may be trusted.
The S3 trusts its chip, so its only gate is the range validator.
The P4 and the knob trust the last fix: restored digits are plausible but untrusted by design.
The flag has two raisers: the SNTP sync callback and the manual time entry that mirrors it.
As a result, a never-fixed clock can show the moon on the S3, never rings an alarm on the P4, and never shows a wrong moon on the knob.

Four facts from the code carry the chain.

The boot order is the trust order (``main.cpp``).
WiFi comes first, then ``settings_store_load`` (:508).
The store loads before ``clock_init`` because the timezone comes from the store and must be set before any moon use, as the comment in ``clock.c``'s epoch function says.
Then the boot NTP runs synchronously inside ``clock_init`` (:512, timeout ``CLOCK_NTP_TIMEOUT_MS`` = 10 s).
The radio is dropped before the panel wakes, because an associated modem's DTIM beacon steals QSPI DMA completions.
The boot sync is done and gone before the first flush.

Two plausibility floors are parsed from the code, not invented.
The NVS restore accepts only ``epoch >= 1609459200``; anything before 2021 is junk.
A system clock that is still invalid lands on the default 2026-01-24 12:00 (``clock_sntp.c``'s fallback chain).
The knob's deep-sleep alarm gate uses its own floor, ``EPOCH_PLAUSIBLE_MIN`` 1600000000 (``knob_power.c``).
A restored but never-fixed instant arms the static moon through the wall bool ("a restore happened"), never through the raw clock's plausibility.
The trust flag stays down until an SNTP fix; the code's own words are that "a factory default clock never shows a silently-wrong moon".

The moon reads time through one function, ``clock_get_utc_epoch`` (``clock.c:257``).
It reads the digits (an I2C transaction on the S3, ``time()`` on the P4 and knob) and runs ``validate_time`` (year 2000..2099 plus the field ranges).
An implausible answer returns ``ESP_ERR_INVALID_STATE``.
Otherwise ``mktime`` turns the local digits into the UTC epoch under the TZ that ``clock_sntp_init`` set from the store.
Five call sites in ``moon_ui.c`` (start, boot replay, the core-1 event scan, the day tick, the wake stale re-bake) skip or defer on a reject.
The function's comment states the reason: "Reject implausible dates so callers (moon phase) show nothing rather than a wrong phase".
The visible half is a black panel on a blank RTC.

The flag gates what hurts to get wrong.
The P4's software alarm poll fires only while trusted, so the default 2026-01-24 12:00 can never ring a phantom alarm (``clock_interrupt.c:183``).
The P4's time label sits at ``LV_OPA_50`` until the flag flips (``screens.c:3813-3825``, compiled out on the S3).
The manual-entry commit (``screens.c:2924``) sets the time, the flag and the day tick together.
``clock_tz_apply`` rewrites the S3's RTC local digits epoch-consistently (``mktime`` before the environment change) or re-derives the P4's, then re-arms the scheduler.

The three graphs below draw the chain from the code: first the overview, then the bases with their fallbacks, then the trust raisers.

.. graphviz:: ../_graphs/time-chain.dot
   :alt: Three time bases feed one system clock; only the fixers raise the trust flag.
   :align: center
   :caption: The overview. Three bases feed one system clock, only the fixers raise the trust flag, and the moon reads time through ``clock_get_utc_epoch`` (``clock.c:257``) alone.

In the overview, TZ comes from the store before any moon use (``clock_sntp.c:134-137``; the default Europe/Berlin is ``CLOCK_DEFAULT_TIMEZONE``).
``clock_get_utc_epoch`` calls ``clock_get_time`` (I²C read on the S3, ``time()`` on the P4 and knob), then ``validate_time`` (:271, year 2000..2099 plus field ranges).
Any implausible answer returns ``ESP_ERR_INVALID_STATE``; otherwise ``mktime`` reads the digits as local time (TZ, DST) and returns the UTC epoch.
Every call site obeys the reject.
``moon_ui_start`` (``moon_ui.c:2790``) with no time leaves the panel black ("a blank RTC leaves the moon black until a trustworthy time exists", ``main.cpp:675``).
Boot replay's final leg, the core-1 event scan, the day tick and the wake stale re-bake each skip or defer, so nothing bakes from the default 2026 date.

.. graphviz:: ../_graphs/time-chain-bases.dot
   :alt: The three time bases and the fallback chain.
   :align: center
   :caption: The bases: the S3's PCF85063A, the P4's system clock with its NVS snapshot, the knob's NVS-only clock, and the fallback chain (``clock_sntp.c:219-243``).

The S3's PCF85063A sits on the shared BSP I²C at address 0x51 (the RTC's own address, unrelated to the panel's 0x51 brightness command), with INT on TCA9554 EXIO3 (``clock.c:121/:128``).
It holds local digits, is battery-backed, and trusts itself by design: the flag is compiled out of its consumers.
The P4 runs the system clock over the internal RTC timer plus SNTP (the curated pin is the external 32k crystal; see the bench note below).
It saves the NVS ``clock``/``epoch`` snapshot on every SNTP fix and every 15 min from the minute tick (``CLOCK_NVS_SAVE_INTERVAL_SEC`` 900).
That snapshot is its only memory across a power loss once the crystal domain dies.
The knob has no RTC.
It follows the same NVS discipline, adds a save before every deep-sleep entry (``knob_power.c:196``), and does a one-time adopt and promote of the older ``knob_time`` namespace (``main.c:220-223``).
The restored instant is the last known time, never trusted.
The fallback chain (``clock_sntp.c:219-243``) runs: now < 1609459200 (pre-2021, junk), then the NVS restore (2021 or later, or nothing), then the default 2026-01-24 12:00.
Both floors are parsed from the code; the knob's rule gate uses its own 1600000000 (``knob_power.c:56``).
The fallback's ``settimeofday`` does not raise the trust flag.

.. graphviz:: ../_graphs/time-chain-trust.dot
   :alt: The trust raisers and what the trust flag gates.
   :align: center
   :caption: The trust raisers (boot NTP, the SNTP callback, the P4's manual entry) and the legs the flag gates.

Boot NTP is synchronous inside ``clock_init`` (``enable_ntp_sync`` at ``main.cpp:512``, timeout ``CLOCK_NTP_TIMEOUT_MS`` 10 s).
It lands before the panel wakes; ``wifi_manager_deinit`` follows at :541, because an associated modem's DTIM beacon steals QSPI DMA completions.
The success path writes the S3's RTC and brings up home with a fresh clock.
``sntp_sync_time_cb`` (``clock_sntp.c:109``) raises the flag, force-snapshots the epoch on non-PCF builds and releases the waiter.
``needs_sync()`` (``CONFIG_CLOCK_NTP_SYNC_INTERVAL_HOURS``, 1 h) re-arms the hourly gate.
Manual entry exists on the P4 only (the hub's clock icon).
Its commit runs ``clock_set_time``, ``clock_sntp_set_time_trusted(true)`` and ``moon_ui_day_tick`` (``screens.c:2924-2927``); a user-entered time is trusted, and the rejected path logs "manual time … rejected".
Two legs read the flag.
The knob arms its static phase on the wall bool ("a restore happened") rather than raw plausibility.
The P4's ring needs ``clock_sntp_time_trusted()`` and either the one-shot flag or ``now >= next_epoch``, so the default 2026-01-24 can never ring a phantom alarm (``clock_interrupt.c:183``).

A bench note on the P4's clock source.
The curated ``sdkconfig.defaults.esp32p4`` pins ``CONFIG_RTC_CLK_SRC_EXT_CRYS=y``.
The defaults file's own comment gives the reason: "the internal RC oscillator is only a few % accurate and is unusable as a clock time base" (see :doc:`the P4 board <../devices/p4>`).
``-B build/p4`` treats the live ``sdkconfig.p4`` as authoritative.
A stale ``CONFIG_RTC_CLK_SRC_INT_RC=y`` pin in that gitignored file shadows the default, because a hand-edited live ``sdkconfig`` line beats every ``default`` line (the pin trap in CLAUDE.md).
The generated ``build/p4/config/sdkconfig.h`` shows which one won.
Grep the live file before you believe a built binary runs on the crystal; the remedy is to delete the stale pin, not to edit the docs.

The 90° orientation steps
-------------------------

The screen does not follow the tilt smoothly.
It waits until the device has clearly been set down at one of four angles (0, 90, 180, 270°) and then snaps there in one atomic flush.
A continuous rotation was rejected on purpose.
It would re-render the whole screen through every intermediate angle, and each of those in-between frames is the half-picture the atomic rule forbids.

The orientation is quantized to **four 90° steps** (0/90/180/270° clockwise), applied as the LVGL root's ``transform_rotation``.
The pipeline runs on the LVGL task.
It reads the IMU itself once per 100 ms through a gate on the UI tick, at its own 10 Hz, independent of the tilt monitor's 1 Hz timer.

1. **Sample**: 100 ms x/y gravity samples, int4-quantized.
2. **Window**: a 1 s windowed mean (10 samples) removes a single-sample spike.
   The mean must stay within one int4 level for about 1.5 s before it counts as stable.
3. **Quadrant**: the integer dot and cross of the stabilized vector with the anchor's home axis give the candidate step.
   The step applies in one ``lv_refr_now``; the atomic rule covers a rotation as much as a screen switch.
4. **Sustained confirmation**: a step candidate must persist across samples before it is adopted.
   A new home pose is learned only when the step is 0 (the device flat, home orientation).

Read the funnel by what each stage refuses.
*Sample* refuses to trust a single reading.
*Window* refuses a moving target: no decision while the average still wanders.
*Quadrant* refuses trigonometry and picks the quarter with integers alone.
*Confirmation* refuses to flip on one stubborn sample.
The pose must pass all four before the screen moves, and then it moves once.

Steps also cost less than a ramp.
A continuous ramp rotates the whole screen (the LVGL root transform) through every intermediate angle.
At 50 ms ticks that is a full-panel re-render per step, and a partial rotation is the half-frame failure the atomic rule exists to prevent.
Four discrete poses, each applied atomically, are cheaper and correct.
The root transform is the only rotation the panel sees (:doc:`the display pipeline <display>`).

Two calibration facts carry the design.

The **mount offset** (Kconfig ``UI_ORI_BASE_V10``) places "upright" where the housing sits on the desk, whatever the panel's own 0° is inside it.
All three panels (the S3 AMOLED, the P4 and the knob) are mounted the same 90° clockwise off the bezel's 12 o'clock, so the default is the single value 900 (+90°), verified on the bench.
The per-clock fine trim (the **ZERO ANGLE** roller in settings) adds on top of this shared base.

Only the flat pose may redefine home.
A **180° flip** re-tracks the home axis only from step 0.
If the firmware adopted a home mid-flip, the flip's passing pose would become the new home: the "the clock woke up sideways" bug class.

The idle and sleep state machine
--------------------------------

Picture a three-rung ladder walked by two timers.
The device sits awake on the home screen.
When the activity timer expires (no touch, no sustained tilt, 60 s by default) it steps down to light sleep.
In light sleep the moon freezes on its current phase, the clock fades to 20 % opacity, and the panel dims to 25 % through its own brightness command, rewriting no pixels.
At that moment a one-shot countdown is armed; when it fires, the device steps to deep sleep.
Deep sleep hides the clock, shuts down the WiFi radio and turns the backlight off, while the RTC crystal keeps counting so the alarm can ring later.

Every number here (the timeout, the countdown, the two dim percentages) is a user setting.
Each timer also has an off switch on its settings screen (the power-off kill switch on :doc:`the idle-timeout screen <../ui/settings-light-sleep>` and its deep-sleep twin).
With the idle rung off the ladder never starts.
With the countdown rung off the device stays dimmed in light sleep indefinitely.

The state machine lives in ``clock_idle.c`` (the activity timer) and ``actions.c`` (the transitions).
Activity is a touch or a tilt departure (see `The tilt: activity and wake`_).
The timer's timeout (``settings_idle_timeout_sec``, default 60 s, user-tunable 60–1800 in settings) triggers light sleep.

.. figure:: ../img/screens/settings_light_sleep.png
   :alt: The idle-timeout settings screen with a seconds wheel.
   :figclass: align-center

   The **idle timeout** setting: the wheel picks the seconds before light sleep, and the X commits and returns.

Light sleep
^^^^^^^^^^^

``action_switch_to_light_sleep_screen`` does four things.

- The moon tick pauses on a static frame (:doc:`the animation pipelines <animations>`).
  ``moon_ui_pause(true)`` interrupts the orbit and parks the last baked viewport.
- The clock widget drops to ``UI_CLOCK_ALPHA_PCT`` (production: **20%** alpha), and the seconds timer pauses with the tick.
- The panel ramps to ``UI_LIGHT_SLEEP_DIM_PCT`` (production: **25%**) over the fade duration (``moon_ui_panel_dim_ramp``).
  This is the same 0x51 ramp as the screen-switch fade, serialized on ``s_panel_mtx``; ``s_fade_busy`` keeps 0x51 ramps mutually exclusive.
  The dim is the sleep transition.
- The deep-sleep countdown is armed at the entry (``clock_timer_enable(settings_deep_sleep_countdown_sec(), ...)``).
  The RTC one-shot counts from the moment the device goes quiet; there is no animation to wait for.

The **countdown kill switch** (``settings_deep_sleep_countdown_enable()``, the deep-sleep screen's power-off toggle) wraps this single arm site.
When it is off, the one-shot is never armed and the device **stays dimmed in light sleep indefinitely**.
The entry logs "not armed, staying in light sleep", and the touch and tilt wake paths still bring it home.
The **idle kill switch** (``settings_idle_timeout_enable()``, the light-sleep screen's toggle) is read live in ``clock_idle_check_cb`` on every 1 s check.
The idle rung stops firing on the next check.
Turning it back on cannot cause an instant sleep, because the tap that flipped the toggle already reset the activity clock.

.. figure:: ../img/screens/light_sleep.png
   :alt: The light-sleep screen: a static moon and a faint clock.
   :figclass: align-center

   The **light-sleep** screen: the static current-phase moon, the panel ramped to 25 %, and the clock at 20 % alpha still showing the minutes.
   The seconds timer paused at the entry.

On the real glass the 25 % is a hardware dim (the 0x51 ramp), so the whole panel, moon and digits, glows at a quarter of the home screen's brightness.
The flat capture above cannot show that; the simulated composite below can.

.. figure:: ../_static/photos/sim_light_sleep.png
   :alt: Simulated light-sleep render at 25 percent hardware brightness.
   :figclass: align-center

   Light sleep on the Waveshare ESP32-S3-Touch-AMOLED-1.75, simulated at 25 % hardware brightness.
   The lit side still reads as a moon, the AMOLED black stays black, and the whole image is a quarter as bright as the home screen.

Deep sleep
^^^^^^^^^^

``action_switch_to_deep_sleep_screen`` turns off everything that keeps a screen live:

.. code-block:: c
   :caption: components/ui/actions.c (deep-sleep entry, abridged)

   g_asleep = true;
   if (!g_wifi_deinited && wifi_manager_is_connected()) {
       wifi_manager_deinit();
       g_wifi_deinited = true;
   }
   g_idle_timer_active = false;
   moon_ui_pause(true);              /* the moon stays at its static phase */
   clock_timer_disable();            /* the countdown already fired */
   loadScreen(SCREEN_ID_DEEP_SLEEP);
   lv_obj_add_flag(objects.deep_sleep_clock, LV_OBJ_FLAG_HIDDEN);  /* clock gone */
   ui_tick();                        /* one last atomic flush (the dark side) */
   if (!g_backlight_off) {
       if (moon_ui_panel_brightness_set(0) == ESP_OK)
           g_backlight_off = true;   /* the panel backlight is OFF */
   }
   lv_timer_pause(g_ui_tick_timer);
   lv_timer_pause(g_seconds_timer);

The tick, the seconds timer, the clock update (through the backlight gate) and the WiFi radio all stop.
The RTC crystal keeps running, which is what the alarm wake relies on.

.. figure:: ../img/screens/deep_sleep.png
   :alt: The deep-sleep screen: the static moon under a crescent sleep glyph.
   :figclass: align-center

   The **deep-sleep** screen: the moon at its static phase under the crescent sleep glyph.
   This capture is the frame just after the entry; the code then hides the clock label and turns the backlight off, so the panel goes black right after it.

.. figure:: ../_static/photos/sim_deep_sleep.png
   :alt: Simulated last lit frame before deep sleep.
   :figclass: align-center

   Deep sleep's last lit frame on the Waveshare ESP32-S3-Touch-AMOLED-1.75: the static moon and the crescent glyph.
   One backlight write later the panel is off until the next wake.

The two clocks differ here.
The P4 (Waveshare ESP32-P4-WIFI6-Touch-LCD-XC) has no PCF85063A ALM wake, and its app-level deep sleep does not call ``esp_deep_sleep``; the crystal runs through it.
So the P4 **stays in light sleep while an alarm is pending** (dim panel, dark side) instead of dropping to the backlight-off state.
The reason is a power decision: an alarm must be able to ring.
The S3 (Waveshare ESP32-S3-Touch-AMOLED-1.75) needs no such guard, because its ALM pin wakes the deep state for real.

The sleep ladder in code: awake, light, deep
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The idle timeout (``settings_idle_timeout_sec``, ``CLOCK_IDLE_TIMEOUT_SEC`` default 60 s) drops the awake device onto the light-sleep screen.
The radio goes down, the UI tick slows to ``UI_LIGHT_SLEEP_TICK_MS`` (100 ms), and the clock container drops to ``UI_CLOCK_ALPHA_PCT`` (20 %) over the 0x51 panel ramp to ``UI_LIGHT_SLEEP_DIM_PCT`` (25 %).
The light-sleep entry arms the deep-sleep countdown (``settings_deep_sleep_countdown_sec``, ``UI_DEEP_SLEEP_COUNTDOWN_SEC`` default 60 s), so sleep depth follows the RTC timer, never an animation.
The deep rung has two guards.
Demo mode refuses the transition and disarms the source.
The P4 with a pending alarm stays on the lighter rung, as described above.
Each rung has its own kill switch, the power-off toggle on its sleep screen.
``idle_timeout_enable`` gates the idle rung live in ``clock_idle_check_cb``, so the ladder never starts.
``deep_sleep_countdown_enable`` gates the arm site, so the one-shot is never armed and the device stays dimmed in light sleep.
The knob runs the same ladder with hardware rungs (:doc:`knob display <knob-display>`): an LEDC backlight dim (GPIO 47) for the dim rung, and ``esp_deep_sleep_start`` with an RTC TIMER wake for the deep one.
Both knob rungs read the same two flags: with them off, the dim rung skips and the deep rung never fires.

The two graphs draw the entry order, the arm at entry, both guards and the wake tail that climbs back.

.. graphviz:: ../_graphs/sleep-entry.dot
   :alt: The sleep-entry ladder from awake to light sleep to deep sleep.
   :align: center
   :caption: The ladder: the idle timeout fires the light-sleep entry, the entry arms the deep countdown, and the countdown fires the deep entry.

``main.cpp:625`` registers ``action_switch_to_light_sleep_screen`` on the idle timeout.
The period is ``settings_idle_timeout_sec`` (``CLOCK_IDLE_TIMEOUT_SEC``, default 60 s, 60..1800), reset by any tap or tilt.
The rung itself is gated live by its kill switch, ``idle_timeout_enable``, in ``clock_idle_check_cb``, so a switched-off idle timer never starts the ladder.
The light entry sets ``g_asleep`` true and calls ``wifi_manager_deinit`` (the radio down: the airplane half of the name).
It slows the UI tick from 10 ms to ``UI_LIGHT_SLEEP_TICK_MS`` (100), pauses the seconds timer, turns the idle monitor off and calls ``moon_ui_pause(true)``.
Then it arms the deep countdown at entry (``clock_timer_enable(settings_deep_sleep_countdown_sec, 1 Hz)``).
The countdown kill switch ``deep_sleep_countdown_enable`` wraps that call; when it is off the device stays dimmed in light sleep and wakes only through the usual doors.
The light screen shows the clock container at ``UI_CLOCK_ALPHA_PCT`` (20 %) over the 0x51 ramp to ``UI_LIGHT_SLEEP_DIM_PCT`` (25 %).
The countdown ticks in the PCF85063A timer (S3) or ``esp_timer`` (P4).
The deep entry hides the clock container (``LV_OBJ_FLAG_HIDDEN``).
It re-applies the flag on every entry, so a wake to another screen cannot leak visible digits.
It then pauses the moon, disables the spent countdown and calls ``moon_ui_panel_brightness_set(0)``.
The backlight is off (``g_backlight_off``), and the RTC clock update is gated on it.

.. graphviz:: ../_graphs/sleep-entry-guards.dot
   :alt: The guards on the deep-sleep entry: demo mode and the P4 pending alarm.
   :align: center
   :caption: The guards on the deep entry: demo mode refuses it, and the P4 with a pending alarm returns to the light screen.

``main.cpp:629`` dispatches ``action_switch_to_deep_sleep_screen`` on the LVGL task when ``settings_deep_sleep_countdown_sec`` expires (``UI_DEEP_SLEEP_COUNTDOWN_SEC``, default 60 s, 60..1800; the settings-deep-sleep picker writes it).
Demo mode refuses the transition and calls ``clock_timer_disable``, because a black panel mid-recording would ruin the take.
The P4 with a pending alarm returns to the light screen instead.
Its deep state is app-level (the crystal keeps running), so the software one-shot could otherwise ring from a dark panel.
The device stays in the lighter state with the one-shot still armed.

The third board runs a different ladder
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The knob shares the look of the ladder but has none of the clocks' instruments.
It has no IMU to feel a pick-up and no RTC to arm, and, being battery-powered, it really switches itself off.
It dims by LEDC duty instead of the panel command and calls the real chip power-down.
The chip's own timer answers the alarm question.
Before sleeping, the knob arms a wake-up for the next alarm rule.
On waking it recomputes that rule from the time it last saved to flash.
Inside the 300 s grace it shows the silent alarm visual.
Within 900 s it holds at 1 Hz until the rule arrives (**never ring early**).
Further off, or with an implausible time, the wake was a dial turn.
Every wake re-corrects over the network, so the timer's drift never compounds.

The knob (:doc:`the Knob board <../devices/knob>`) shares the screens and the scheduler but not the clocks' state machine: no IMU to wake on, no RTC to arm, no light-sleep screen.
Its ``knob_power`` supervisor dims by LEDC duty (``KNOB_DIM_DUTY`` 64, the clocks' 25 % panel rung).
The shared clock widget drops to ``UI_CLOCK_ALPHA_PCT`` (20 %), the same dimmed look as the clocks' light sleep.
Then it calls the real ``esp_deep_sleep_start``; the clocks' app-layer deep sleep never powers down, while a portable knob has to save its battery.
Both rungs read the same ``settings.txt`` store pair the clocks' sleep screens own: ``idle_timeout_sec`` for the dim, and one ``deep_sleep_countdown_sec`` later for the sleep (the clocks' additive posture).
So the shared sleep-screen rollers edit the idle behaviour of all three boards, and so do the two kill switches.
With ``idle_timeout_enable`` off the dim rung is skipped and the board stays at full brightness; if it was already dimmed at the toggle, the next supervisor pass brings the backlight back.
With ``deep_sleep_countdown_enable`` off it dims and **stays dim**, never calling ``esp_deep_sleep``; real activity wakes it.
With both off the knob stays awake and undimmed.

Where the P4 answers the alarm question by staying in light sleep, the knob uses the **S3 internal RTC timer**.
Before sleeping it arms ``esp_sleep_enable_timer_wakeup`` for the next fixed rule, alongside the EXT0 (dial-A) and EXT1 (tap) pads.
A cause-4 wake is arbitrated against the rule recomputed from the NVS epoch snapshot.
Within the grace (300 s) it shows the silent alarm visual; early (≤ 900 s) it holds at 1 Hz until the rule (**never ring early**); further off or implausible it is a normal dial-style wake.
RC drift is the accepted cost (about 0.5 % worst case).
It never compounds, because every wake re-corrects via SNTP and re-arms from the fresh instant.
The full design, the ladder and the arbitration, is in :doc:`the knob display <knob-display>`.

The RTC alarm disarm rule
-------------------------

The RTC chip remembers its armed alarm through a reboot, because the register is battery-backed.
The scheduler rewrites that memory on every schedule.
A device that boots with no alarm rules must also erase it.
Otherwise a stale time left from an earlier boot still fires, and a device with no alarms wakes at the wrong minute showing a rule that no longer exists.
That erase on zero rules is the invariant, and the headless device is the honest test: a phantom wake never comes back to the desk.

The PCF85063A's alarm registers **persist across reboots**: the armed time, the AIE bit and the AF latch.
The scheduler writes the next alarm time on every schedule.
A boot with **zero alarm rules** must call ``clock_clear_alarm()`` explicitly.
Without it, a stale armed time fires a **phantom alarm**: the ALM pin toggles, the device wakes from deep sleep at the wrong minute and shows the alarm screen for a rule that no longer exists.

Three legs of the invariant are easy to miss.

- The boot's ``init`` leg disarms even when ``alarm.txt`` is **missing** (``ESP_ERR_NOT_FOUND``).
  A fresh flash wipes the file, but the PCF's arm survives on the chip, so a missing-file boot is a zero-rule boot too.
- The disarm clears the **AF flag itself**, not only AIE (``clock_enable_alarm(false)`` clears AF on the way out, symmetric with the enable branch).
  AIE gates only the INT pin output; the PCF still latches AF when the clock matches the ALM registers with AIE=0.
  The S3's 1300 ms poll reads the raw CTRL2 flags rather than the pin, so a latched AF that survived a deactivation in settings would fire the alarm screen once on the next poll.
- The S3's poll fires only when the scheduler's cache holds a next rule (``clock_alarm_get_next_scheduled``), the same gate the P4's branch has (the P4 also requires trusted time).
  A latched AF with no scheduled rule is logged and cleared, never fired.
  Without that gate, a stale latch rings with zero rules, and a hung LVGL task plus a watchdog reset re-fires it on every boot, pinning the device on the alarm screen.
  A real deep-sleep ring is unaffected: boot loads the rules and ``schedule_next`` fills the cache before the first poll.

The three graphs draw the chain from the code: the overview, then the rule write, then both boards' arms, the shared 1300 ms poll, and the action it lands in.

.. graphviz:: ../_graphs/alarm-lifecycle.dot
   :alt: The alarm lifecycle overview: one scheduler, two board arms, one action.
   :align: center
   :caption: The overview. One scheduler writes both boards' arms (the S3's ALM pin, the P4's ``esp_timer`` one-shot), and both fires land on ``action_alarm_triggered`` in the LVGL task. The poll re-arms the next fire, so a dismiss only ends the show.

.. graphviz:: ../_graphs/alarm-lifecycle-write.dot
   :alt: How a rule write reaches the alarm arm.
   :align: center
   :caption: The write: every rule change goes through ``clock_alarm_scheduler_write`` and ``clock_alarm_schedule_next``.

``clock_alarm_set_pomodoro_interval`` (``*/N * * * *``), ``clock_alarm_set_clock_alarm`` (``M H * * dow``) and ``clock_alarm_deactivate_all`` ("# no alarms") all go through ``clock_alarm_scheduler_write``.
It rewrites ``/spiffs/alarm.txt`` whole: the file is the rule set, reloaded on boot and armed from it.
The entries are mutually exclusive, so one rule kind exists at a time.
``clock_alarm_schedule_next(current)`` is the single arming funnel, and it also runs after every fire.
Its zero-rules branch runs ``clock_clear_alarm`` (AIE, the ALM registers and the AF latch, since a latched AF alone would still fire the poll) and ``disarm_esp_timer``.
That is the disarm invariant above; the boot runs the same leg when ``alarm.txt`` is missing.
Its branch for one or more rules caches the next time and calls ``clock_set_alarm`` plus ``clock_enable_alarm(true)``.
On the S3 that writes the PCF85063A ALM registers and AIE; on the P4 the same call is a validated no-op and the one-shot carries the fire.
``clock_trigger_alarm_update`` feeds the home row.
The off half of the write path fires it too, so a deactivate-all shows ``--:--`` at once.

.. graphviz:: ../_graphs/alarm-lifecycle-fire.dot
   :alt: The alarm fire paths on both boards and the shared poll.
   :align: center
   :caption: The fires: the S3's ALM pin and the P4's one-shot both resolve inside one shared 1300 ms poll that calls the alarm action on the LVGL task.

On the S3 the ALM pin sets AF and drives the TCA9554 INT to EXIO3 (``IO_EXPANDER_PIN_NUM_3``, the same line the deep-sleep wake uses).
``clock_handle_interrupts`` reads the interrupts and, gated on the scheduler's cache (``clock_alarm_get_next_scheduled``), triggers, clears AF and re-schedules.
A latched AF with no rule scheduled is a stale latch, cleared without firing, matching the P4.
On the P4 the one-shot's callback only sets a flag on the esp_timer task; nothing UI runs there.
The poll branch accepts the flag or a wall-clock backup check (``now >= next`` catches a one-shot that a time change left stale), gated on the trust flag, then stops the one-shot and re-schedules.
Both branches run inside one shared poll, ``lv_timer_create(clock_rtc_interrupt_handler_cb, RTC_POLL_INTERVAL_MS = 1300)``.
Its ``clock_cb.c`` dispatchers call the registered actions directly, in the LVGL task.
The ring relights the backlight, sets ``g_alarm_active`` and calls ``alarm_audio_ring_start`` (an ES8311 gradual ramp; the knob is the codec-less silent board).
It sets ``g_asleep = true`` (dismissing the ring is a wake event), sets the tick to 10 ms and calls ``moon_ui_pause(true)``, since a screen that shows no moon needs no bake.
Then it calls ``clock_timer_disable`` and ``clock_idle_reset_timer`` (no auto-sleep while ringing) and ``loadScreen(SCREEN_ID_ALARM)``.
For the dismiss, ``alarm_indev_read_cb`` fires on every press regardless of hit-test, but only while ``g_alarm_active`` and ``!clock_is_idle()``.
A press while asleep goes to the earlier-registered idle-wake callback instead.

The interval (pomodoro) rule's settings side is drawn in :doc:`the pomodoro interval <../ui/settings-pomodoro>` (``pomodoro-cycle.dot``).
It covers the three-glyph entry screen, the twin-roller screen whose RELEASED handler is the commit, and the midnight-anchored ``*/N`` day-grid arithmetic.
The lifecycle above is where both rule kinds land.

The tilt: activity and wake
---------------------------

The accelerometer feeds two decisions; this section covers the second: is someone handling the device?
The answer cannot be "the numbers moved", because a desk clock that wakes at every footstep has a flat battery by morning.
So the machine keeps a slowly following estimate of the pose the device has settled into.
It counts activity only when the readings sustain a move away from that pose: far enough, in a consistent direction, for a couple of samples in a row.
The pose the device fell asleep at is the reference, and motion relative to it is the wake.

The tilt sensor (QMI8658C) drives three things.
The first is the **activity/wake** signal.
The second is the whole-screen **90° orientation** pipeline (see `The 90° orientation steps`_), the tilt's only UI coupling while the orbit is off.
The third is the moon's out-of-plane orbit (:doc:`the animation pipelines <animations>`).
``UI_MOON_ORBIT_ENABLE`` defaults to ``n`` because the ~2 s relief-DEM re-bake cannot keep up with the tilt cadence.
The wake fires on **departure from a settled pose**, not on raw per-sample deltas:

- The firmware tracks the settled pose as an EMA of the gravity vector.
- A departure has to clear one of **two bars**, per-state thresholds on the same angle: ``UI_IMU_ACTIVITY_DEG`` (5°) while awake, and the deliberate pick-up ``UI_IMU_WAKE_DEG`` (20°) while asleep.
  It must hold for a **2-sample streak** before it counts as activity.
  While asleep, the band between the bars neither wakes nor holds.

The design prevents the device from waking right after it slept.
A device on a desk, jostled by a fan or a footstep, sits at a slightly different pose than the EMA it fell asleep at.
A raw-delta wake would fire the moment the sleep screen appeared.
The departure model treats the settled pose as the reference and wakes only on a sustained move away from it.
That fixes the "the clock woke itself" bug class at the signal level, with no sleep debounce.

The three graphs draw the chain from the code.
One read path serves two consumers on two clocks: the monitor's 1 Hz timer and the orientation pipeline's 100 ms gate on the UI tick.
The settled state is the single edge between them, and the 2 s press re-sync carries its own stability and on-axis gates.

.. graphviz:: ../_graphs/tilt-pipeline.dot
   :alt: The tilt chain overview: one IMU read path, three consumers.
   :align: center
   :caption: The overview. The QMI8658C serves one burst-read path; ``imu_is_ready()`` gates every consumer, and the moon's orbit consumer is off by default.

The QMI8658C sits on the shared I²C (0x6A/0x6B).
The knob ships ``imu_stub.c`` (ready=false) and excludes ``moon_ui.c`` entirely, so its UI sits at the base orientation and tilt never wakes it.
Each sample is banded on ``|g|``: outside 0.5..1.5 g it is a shake or drop and the firmware drops it.
A NACK-stuck bus logs the first and every 20th consecutive failure.
The third consumer, the moon's out-of-plane orbit (``moon_imu_tick``, ``moon_ui.c:2303``: asin(−gz), low-pass, dead zone, clamp, quantize, re-bake), is off by default.
``UI_MOON_ORBIT_ENABLE`` is ``n`` because the ~2 s relief-DEM re-bake cannot keep up with the tilt cadence on the S3, so orientation is the tilt's only UI coupling.

.. graphviz:: ../_graphs/tilt-pipeline-monitor.dot
   :alt: The 1 Hz activity and wake monitor.
   :align: center
   :caption: The 1 Hz monitor (``moon_tilt_monitor_cb``): two EMAs, the departure angle, the two bars and the 2-sample streak.

``moon_tilt_monitor_cb`` runs its own 1 s ``lv_timer`` (``moon_ui.c:2729``), deliberately separate from the moon tick.
The moon tick is paused in sleep, and the wake check must run there.
With ``CONFIG_UI_IMU_ENABLE=n`` the timer survives only to cache the LVGL task handle.
A fast EMA (α 0.5) gives ``s_pose_g``, unit-normalized; the first sample seeds both EMAs, so the first departure is zero.
Departure is ``acos(dot)`` between the pose and the settled reference, in degrees, never a raw sample delta.
The at-rest noise floor (p90 about 3.3°) is why raw deltas fired "woke right after sleep entry".
Below ``UI_IMU_ACTIVITY_DEG`` (5°) the pose counts as settled, and the slow EMA (α 0.2) absorbs it into the reference.
It absorbs only while settled, so the at-rest drift of about 0.4–0.7°/s is tracked rather than fought.
The drift ceiling is the bar ×α/(1−α) = 1.25°/s; α 0.1 sat at its ceiling on a hot board.
The two bars are per-state thresholds on the same departure.
Awake, the 5° activity bar holds the idle timer.
Asleep, the deliberate pick-up bar ``UI_IMU_WAKE_DEG`` (20°) cannot be cleared by table vibration or drift; the band from 5 to 20° does neither.
A streak of 2 consecutive qualifying samples gates both bars.
The 1 Hz call site caps ``clock_idle_notify_tilt_activity`` (``clock_idle.c:92``) at one call per second.
That function always resets the idle stamp and, when asleep, opens the tilt door of ``wake-doors.dot``.

.. graphviz:: ../_graphs/tilt-pipeline-orient.dot
   :alt: The 10 Hz orientation pipeline from sample to screen rotation.
   :align: center
   :caption: The orientation pipeline (``screen_orientation_apply``): quantize, average, check stability, pick the quadrant, confirm, rotate every screen root.

``screen_orientation_apply`` runs on every 10 ms UI tick but samples only through the 100 ms gate (``ORI_SAMPLE_MS``), so it runs at 10 Hz; the monitor's 1 Hz timer does not feed it.
x and y are int4-quantized (``ORI_Q_SCALE`` 8: one level is 1/8 g, clamped to ±1 g).
A flat gate below 0.25 g leaves the in-plane pose undefined, and the orientation, window and reference freeze.
The 1 s running mean (``ORI_WINDOW`` 10) is the 1 Hz low-pass.
Stable means the filtered mean holds within one int4 level for ``ORI_STABLE_SAMPLES`` 15 (about 1.5 s).
An integer dot and cross quadrant picks the step from {0, 90, 180, 270}° (clockwise positive), with no atan2 and no floats.
The step is decided before the reference re-tracks, and re-tracking happens only while settled and at step 0.
So a fast 180° flip cannot adopt its passing pose as home (the "clock woke up sideways" bug class).
The step must come out of 10 consecutive samples (about 1 s) before it applies, so a deliberate move costs at most about 1 s.
The applied value is ``v10 = (ORI_BASE_V10 + s_user_base_v10) − step×10``, with the sign from ``UI_CLOCK_ROLL_SIGN``.
The base is 900 (+90°) on all three panels, the single shared panel-mount offset verified on the bench 2026-11-16.
``s_user_base_v10`` is the persisted wall-mount trim (``display_rotation_deg`` ×10), seeded at boot by ``action_orientation_init`` and moved live by the Settings display roller.
``ori_apply_v10`` pushes the LVGL ``transform_rotation`` to all 17 screen roots, pivoting on the centre.
The rotated root's panel-sized RGB565 layer (about 0.4 MB at 466, about 1.0 MB at 720) comes from the CLIB malloc in PSRAM, because the 64 KB LVGL pool cannot hold it.
Hit testing inverse-maps through the transform; step 0 draws directly at zero cost.
The 2 s press re-sync (``action_register_orientation_touch``) acts only if the pose is stable and on-axis (``|dot|² ≥ 3|cross|²``), so a palm released mid-flip is dropped.
When it acts, it re-anchors home and pushes the base orientation.

The wake paths
--------------

Three doors open the same way: a finger (a tap), a hand (a sustained tilt away from the settled pose), and the clock itself (the alarm).
All three run one tail: stop any ring, relight the panel if it was off, and head for the airplane screen.
The one deliberate split is the time check.
The WiFi reconnect and SNTP run on their own background task, never on the LVGL task, so the screen you woke up to can repaint while the radio works.
The moon comes back at its current phase at once, with no replay.
If the stored frame is stale, the replacement bakes in the background and lands atomically when done, for the same reason.

Three wakes share one handler (``action_wake_screen``):

1. **Touch**: the wake tap, a tap on a sleep screen's root.
   A second, deliberate tap opens the settings hub; the wake tap itself fires before the screen swaps.
2. **Tilt departure**: the idle-wake callback from the tilt monitor.
3. **Alarm**: the ALM pin (S3: a real deep-sleep wake; P4: the software one-shot from light sleep).

Each door has its own guard: the idle-state test on the tap, the departure bars and streak on the tilt, and ``g_alarm_active`` with its awake bypass on the ring.
The tail is one handler.
The awake alarm dismiss is the one edge that deliberately skips the ``clock_trigger_idle_wake`` funnel.
A tap while awake otherwise only resets the idle timer, which is the one-way door the callback's comment documents.
How the finger and the dial reach these doors (the one BSP-owned indev with its three co-located listeners, and the knob's own polled read paths) is in :ref:`input-chain-ui <input-chain-ui>`.
The graphs show the overview plus its two ends.

.. graphviz:: ../_graphs/wake-doors.dot
   :alt: The three wake doors funnel into one wake tail.
   :align: center
   :caption: The overview: the three doors funnel through ``clock_trigger_idle_wake`` (``clock_cb.c``; ``main.cpp`` registers ``action_wake_screen`` as the idle-wake callback via ``clock_register_idle_wake_callback``). Only the alarm-screen dismiss skips the funnel and lands on the tail directly.

.. graphviz:: ../_graphs/wake-doors-doors.dot
   :alt: The guard on each wake door.
   :align: center
   :caption: The guards on the touch, tilt and alarm doors.

The touch door is ``indev_read_cb`` (LV_EVENT_PRESSED/PRESSING, registered by ``clock_idle_init``, ``clock_idle.c:70/:71``).
PRESSED while idle triggers the wake; while awake the same tap only resets the idle timer, and that asymmetry is the one-way door.
The tilt door wakes only on departure from the settled-pose EMA: the two per-state bars (5° activity awake, 20° ``UI_IMU_WAKE_DEG`` asleep), a 2-sample streak, at most once per second from the 1 Hz call site.
Raw deltas are the "the clock woke itself" bug class; the full chain is ``tilt-pipeline.dot``.
For the alarm door, ``action_alarm_triggered`` sets ``g_asleep = true`` (the ring screen counts as asleep).
While awake at that screen the idle door cannot fire, so ``alarm_indev_read_cb``, the second indev callback, dismisses anyway (``g_alarm_active && !clock_is_idle()`` calls ``action_wake_screen`` on the same press).

.. graphviz:: ../_graphs/wake-doors-tail.dot
   :alt: The shared wake tail in action_wake_screen.
   :align: center
   :caption: The tail (``action_wake_screen``, ``actions.c``, LVGL task): stop the ring, relight, resume the timers, un-pause the moon, then the airplane screen or the on-demand sync.

``action_wake_screen`` stops the ring first, since every dismiss path runs through it.
It relights the backlight if ``g_backlight_off`` and restores the clock alpha.
It consumes ``g_asleep``; in the stale-alarm branch (was asleep and WiFi off) the wake stays a normal wake and the alarm cannot take it over.
It resumes the 10 ms tick and the seconds timer, disables the countdown, un-pauses the moon and runs ``moon_ui_day_tick`` (re-bake only if the day rolled over).
Then it lands the airplane screen or the on-demand sync.
The sync runs off the UI task on a background task (stack 6144) with ``wifi_manager_init`` plus SNTP, and the UI tick upgrades the WiFi screen to airplane on the done flag; ``wifi-sync.dot`` owns the starters.
The idle re-arm runs only when ``!moon_ui_replaying()``.
A touch during the boot presentation wakes the screen, and the replay's tick end leg re-arms the ladder when the animation is over.

.. figure:: ../img/screens/alarm.png
   :alt: The alarm screen with the time, the alarm glyph and the weekday arc.
   :figclass: align-center

   The **alarm** screen, the third wake, shown while the ring plays; a touch stops the ring and heads home.
   The weekday arc above the clock marks the rule's active days.

The alarm is the wake that matters most to the dark panel: it rings on a backlight that was off.

.. figure:: ../_static/photos/sim_alarm.png
   :alt: Simulated alarm screen on the panel at full brightness.
   :figclass: align-center

   The alarm wake on the Waveshare ESP32-S3-Touch-AMOLED-1.75: the minute and the alarm glyph at full brightness, straight out of deep sleep.

All three doors run the same tail:

.. code-block:: c
   :caption: components/ui/actions.c (wake tail, abridged)

   alarm_audio_ring_stop();                 /* the touch snooze kills the ring */
   if (g_backlight_off)                     /* the deep-sleep panel is lit again */
       moon_ui_panel_brightness_set(100);
   ...
   /* on-demand sync: the transient WiFi screen (the same screen the
    * boot flow uses, so the animation flows across the switch) when
    * not connected or the RTC is due for a refresh; otherwise
    * straight to airplane. */
   if (!wifi_manager_is_connected() || clock_sntp_needs_sync())
       action_start_wifi_sync();
   else
       action_switch_to_airplane_screen(e);

The sync runs on a **background task** (stack 6144), ``wifi_manager_init`` plus SNTP, off the LVGL task.
The UI tick switches from the WiFi screen back to airplane when the done flag lands.
This split prevents the **1.7 s wake freeze**.
A synchronous WiFi reconnect on the LVGL task blocks the task that repaints the wake screen for the whole reconnect.
That freeze is not a crash and not a watchdog event; it is the UI's own task parked inside the radio stack.

The wake shows the **current phase, static** (:doc:`the animation pipelines <animations>`), with no sweep, by design.
A boot can tell the story of how the moon got where it is; a wake is an answer.
When the day has not turned, the wake shows the boot's bake.
When the frame is stale (new day, new event tint, a bake interrupted by sleep), the wake still returns at once.
The re-bake runs on a core-1 task, and the next tick shows the finished frame atomically, for the same reason the radio runs off the LVGL task.

The hourly sync
---------------

Once an hour the device checks its clock against the network, but only if it is already awake and sitting still on the home screen.
A sleeping device is never woken for it; its next real wake syncs anyway.
An open settings session outranks it.
The WiFi screen is up while the sync runs and goes down the moment it lands, whether it succeeded or failed.

The same sync path fires when three things hold: the device is **at rest on the airplane screen** at the top of a minute, it is not connected, and the RTC is due for a refresh (``CLOCK_NTP_SYNC_INTERVAL_HOURS``, production: 1 h).
The policy is conservative:

- At rest only: the firmware never wakes a sleeping device for it, since waking would cost more power than it saves; the next wake re-syncs.
- No interference: a settings session or a sync already in flight gates the trigger off.
- Transient: the WiFi screen shows only while the sync runs; success and failure both drop back to airplane.

Where to go next
----------------

- :doc:`The P4 port <p4-port>`: how the same machine runs on a different panel, a different bus and a remote Wi-Fi core.
- :doc:`The knob display <knob-display>`: the knob's real deep sleep, its timer wake and the alarm arbitration.
- :doc:`The animation pipelines <animations>`: what the moon screens show while this machine decides when.
