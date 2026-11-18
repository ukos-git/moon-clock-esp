The clock and the sleep machine
===============================

The moon pipelines decide what to show; this page's machine decides
*when* — the minute-driven clock, the 90° orientation steps, the
idle/sleep state machine (light sleep = a static dim frame, deep
sleep = backlight off), and the three wake paths (touch, tilt,
alarm). It is
the part of the product where a *clock* and a *moon* have to agree on
every minute of the day.

The short version
-----------------

The whole page answers one question: *when does the screen change,
and who is allowed to change it?* The moon math (the other
implementation pages) decides **what** to show; this machine decides
*when* — and four answers cover everything below:

1. **Two moon screens, one image.** The home screen ("airplane")
   and the WiFi screen carry the *same* moon picture — the WiFi
   screen is that picture plus a transient sync indicator, shown
   only while a time sync is in flight (boot, hourly, manual).
   Because the images are identical, the switch between them needs
   no hiding; everything else — the settings hub and its
   sub-screens — shows no moon at all.
2. **Nothing moves until something changes.** The clock updates
   only when the displayed minute rolls (and never while the panel
   is dark), each digit sits in its own fixed-width box so the
   string never hops, and the screen's orientation snaps between
   *four* 90° poses — never a continuous tilt-follow. Every visible
   change lands as one atomic burst, like every other frame in this
   product.
3. **A ladder of two rungs and two timers.** No touch and no
   sustained tilt for a minute (the default, user-tunable) →
   *light sleep*: everything pauses, the panel dims to 25 % and
   the clock to 20 % opacity — the dim *is* the sleep indicator.
   A one-shot countdown armed at that very moment later fires
   *deep sleep*: the clock is hidden, the WiFi radio is
   deinitialized, the backlight goes off — and only the RTC's
   crystal keeps counting, because that is what lets the alarm
   find its moment. (The knob is the one board that truly powers
   down.)
4. **Three doors, one handler.** Touch, tilt, and alarm all enter
   through the same wake tail: kill any ring, relight the panel,
   show the moon at its current phase immediately — and run the
   network time check on a background task, so the freshly woken
   screen never freezes waiting for the radio.

Everything below is this machine section by section: the screens,
the minute rules, the four poses, the ladder, the alarm invariant,
the tilt, and the doors.

Vocabulary
----------

Sixteen terms carry the page — the ones shared with the rest of the
firmware also have fuller entries in the :doc:`glossary
<../glossary>`:

- **Tick / LVGL task** — the UI worker: wakes every 50 ms, redraws
  what changed, and pushes the finished frame to the panel. Every
  change the eye sees rides on this tick.
- **Activity timer / idle timeout** — the countdown that a touch
  or a sustained tilt resets; when it expires
  (``settings_idle_timeout_sec``, default 60 s, range 60–1800) the
  device steps down the sleep ladder.
- **RTC** — *real-time clock*: a small chip with its own crystal
  (the PCF85063A on the S3) that keeps counting seconds while the
  main chip idles; the clock the alarm and the countdown ride on.
- **ALM pin** — the RTC chip's alarm output wire: when the armed
  alarm instant arrives, the pin toggles and says "wake up and
  ring".
- **One-shot** — a countdown programmed to fire exactly once after
  a chosen delay: the deep-sleep countdown is a one-shot armed the
  moment the device goes quiet, so sleep depth never follows an
  animation.
- **App-level deep sleep** — on the clocks, "deep sleep" is a
  *software* state, not a power-down: backlight off, timers
  paused, radio deinitialized — the chip itself keeps running
  (which is why the P4 can simply stay dim when an alarm is
  pending). The knob's deep sleep is the real thing,
  ``esp_deep_sleep_start`` — silicon off.
- **Airplane (modem-sleep) screen** — the home screen's internal
  name: while it sits there the WiFi modem is *down*; the network
  is woken only briefly, on demand, to check the time.
- **Alpha** — transparency in percent: the clock at 20 % alpha is
  drawn one-fifth as opaque, letting the AMOLED's black read
  through the digits.
- **0x51 brightness ramp** — the panel's own dim/brighten command
  (glossary: *fade-to-black*): it changes every pixel's brightness
  at once and writes no pixels, which is why it fits inside the
  atomic-frame rule.
- **EMA (exponentially weighted moving average)** — a
  slowly-following average: each new sample nudges the running
  value a little, so a one-off jolt barely moves it and a held
  pose gradually becomes "the settled pose."
- **int4 quantization** — rounding each gravity axis to a coarse
  integer grid where one level is 1/8 g (clamped to ±1 g): table
  jitter never counts as motion, and everything downstream is
  integer arithmetic.
- **Dot / cross** — the two integer products that answer the tilt
  geometry without trigonometry: how strongly the sampled direction
  agrees with the home axis (the dot) and which side of it it falls
  (the cross); together they pick the quadrant.
- **Departure wake** *(glossary)* — waking on a *sustained move
  away* from the settled pose, not on raw motion: two per-state
  thresholds (the 5° activity bar awake, the 20° wake bar asleep)
  held for two consecutive samples.
- **SNTP / on-demand sync** — the network time check the device
  runs at boot, hourly, and on wake when the RTC has drifted enough
  to distrust; the radio spins up only for the check's duration.
- **LEDC duty** — the knob's dimmer dial: an ESP32 PWM channel
  whose duty cycle sets the backlight — an 8-bit scale, duty 64
  ≈ the clocks' 25 % panel rung.
- **EXT0 / EXT1 pads, NVS snapshot** — physical wake inputs for a
  chip that is truly asleep (turning the knob, tapping it), and the
  rough current time the knob stores in flash before sleeping, so
  it knows approximately *when* it woke before the network corrects
  it.

The screen model
----------------

**The short version.** Two screens in the whole product carry the
moon, and they carry the *same* image: the home (airplane) screen,
and — while a time sync is in flight — the WiFi screen, which is
that same picture with a transient indicator laid over it. A third
family, the settings hub and its sub-screens, carries no moon at
all. The firmware always knows which family the visible screen
belongs to (``moon_screen_has_image()`` feeding ``s_moon_screen``),
and that knowledge is what makes the switch between the two moon
screens free: same picture, nothing to hide.

The home screen is the **airplane (modem-sleep) screen**: the moon in
the foreground, the clock label, the seconds arc — no icons. The
**WiFi screen** is the *same* moon image plus a transient indicator,
shown only while a sync is in flight (boot, hourly, manual), then
dropped back to airplane. Because both screens carry the same moon
image, the switch between them is the *both-moon* case of the fade
rule (:doc:`the animation pipelines <animations>`): no dim, one atomic whole-panel flush (while the boot sweep existed, this is how its replay flowed seamlessly across the switch).

.. figure:: ../img/screens/modem_sleep.png
   :alt: The airplane (home) screen
   :figclass: align-center

   The **airplane** (modem-sleep) screen — the moon, the minute, the
   seconds arc. No icons.

.. figure:: ../img/screens/wifi.png
   :alt: The transient WiFi screen
   :figclass: align-center

   The **WiFi** screen — the *same* moon image plus the transient
   sync indicator, shown only while a sync is in flight; the switch
   back is the no-dim both-moon case.

The same UI on the second board — the Waveshare
ESP32-P4-WIFI6-Touch-LCD-XC (720×720 round IPS, :doc:`the P4 4C port <../devices/p4>`): the moon
bakes at a larger disk (650 px), the clock digits sit on the 148-px
font, but the screen logic — these transitions, the wake paths, the
sleep states — is the machine this page describes, unchanged.

.. figure:: ../img/screens/p4/modem_sleep.png
   :alt: The airplane screen on the P4 board
   :figclass: align-center

   The **airplane** screen on the P4 board (Waveshare
   ESP32-P4-WIFI6-Touch-LCD-XC) — the same layout at 720×720: the
   moon at 650 px, the 148-px clock digits, the seconds arc.

The **settings hub** (tap on the home or WiFi screen) and its
sub-screens (alarm, pomodoro, alarm time, light-sleep, deep-sleep,
timezone, WiFi) carry *no* moon image. ``moon_screen_has_image()``
checks the active screen's root against the moon images' roots — and
that check feeds ``s_moon_screen``, the moon-screen display gate.
(Since the burn-in spin's removal there is no render task to key off
it; the gate tracks which screen carries the image, and the hourly
refresh probe is its next consumer.)

.. figure:: ../img/screens/settings_hub.png
   :alt: The settings hub
   :figclass: align-center

   The **settings hub** — the sub-screens as a hub-and-spoke around
   the exit. The moon is gone: a hub screen is one of the moon-less
   screens the display gate tracks.

The sub-screens:

.. figure:: ../img/screens/settings_alarm.png
   :alt: The alarm rules screen
   :figclass: align-center

   The **alarm** hub icon: the rule list (add / delete / edit) plus a
   default Mon–Fri 07:00 rule (the project default that replaced the
   old every-5-minute rule).

.. figure:: ../img/screens/settings_alarm_time.png
   :alt: The alarm time screen
   :figclass: align-center

   The **alarm time** screen — the week arc (Mo–Fr here) selects the
   rule's active days; the time wheel picks the hour and minute.

.. figure:: ../img/screens/settings_pomodoro.png
   :alt: The pomodoro repeat-interval screen
   :figclass: align-center

   The **pomodoro** interval — the focus/timer repeat interval in
   minutes.

.. figure:: ../img/screens/settings_deep_sleep.png
   :alt: The deep-sleep countdown settings screen
   :figclass: align-center

   The **sleep countdown** — the light-sleep → deep-sleep seconds
   (the one-shot armed at the light-sleep entry itself).

.. figure:: ../img/screens/settings_tz.png
   :alt: The timezone settings screen
   :figclass: align-center

   The **timezone** screen — one of the 67 curated named zones
   (Europe/Berlin here); committing rewrites the RTC
   epoch-consistently, re-arms the alarm and re-gathers the moon for
   the zone's city (:doc:`the architecture <architecture>`,
   ``clock_tz``).

.. figure:: ../img/screens/settings_wifi.png
   :alt: The WiFi networks screen
   :figclass: align-center

   The **WiFi networks** screen — the scan results (the quiescent
   AP-scan of the on-demand sync, :doc:`the architecture <architecture>`); selecting a network
   opens the password entry with the on-screen keyboard (:doc:`the round keyboard <../keyboard>`).

The minute-driven clock
-----------------------

**The short version.** The home clock is not animated: it changes
exactly when the displayed minute rolls, and the update runs on the
UI worker. Two guards decide whether an update happens at all —
skip it entirely while the panel is dark (the RTC keeps time; the
first wake re-applies it), and, historically, hold a sync until a
boot sweep finished, a guard that retired along with the sweeps.
And the digits never slide sideways, because each character lives
in its own fixed-width box: when a ``1`` is replaced by a ``4``
only that box's ink changes.

The clock display updates are **minute-driven, on the LVGL task**:
``action_update_clock_display`` fires when the displayed minute
changes, writes the clock label, and refreshes the seconds arc — how
late that can land (the parsed 1.3 s poll that notices the rollover,
``clock_interrupt.c:26``) is drawn in :ref:`the freshness ladder
<freshness-ladder-perf>`. Two
gates matter:

- **The backlight gate** — while the panel is off (deep sleep), the
  clock update is skipped entirely (``g_backlight_off``): the RTC
  keeps running (the crystal clock runs through the app-level deep
  sleep), the screen is dark, and the next wake restores the panel
  and the minute update re-applies.
- **The sweep gate** *(retired family-wide)* — an on-demand WiFi
  sync (below) used to be blocked while a boot sweep was playing: it
  would switch screens mid-sweep. The clocks retired the gate with
  their sweep 2026-11-13, and the knob's own gate went with its sweep
  on 2026-11-14 (:doc:`the knob display <knob-display>`) — nothing
  gates the sync but the remaining policy below.

The minute label is drawn as **fixed-width HH:MM slots** — the numbers
font is proportional, so a single label would read 175 px at ``11:11``
and 290 px at ``44:44`` and the whole string would hop sideways on every
minute change. Each character sits in its own centred, fixed-width slot
label instead, so the string is a constant width and only the ink moves.

.. figure:: ../img/screens/settings_time.png
   :alt: The set time/date screen with the fixed-width HH:MM clock
   :figclass: align-center

   The **set time/date** screen — the clock digits are the same
   fixed-width HH:MM slots as the home screen (each character in its
   own centred slot, so the string never shifts), over the date; the
   X commits the time.

The wall clock's chain of trust
-------------------------------

**The short version.** Every minute the clock shows and every phase
the moon bakes traces back to one of **three time bases** — the S3's
PCF85063A, the P4's system clock over the internal RTC timer, the
knob's NVS snapshot — and exactly **one flag** (``s_time_trusted``,
``clock_sntp.c``) decides how far any of them may be trusted. The S3
**trusts its chip** (its gate is the range validator alone); the P4
and the knob **trust the last fix** (restored digits are
plausible-but-untrusted by design). The flag has exactly two raisers:
the SNTP sync callback and the manual time entry that mirrors it. A
never-fixed clock **can** show the moon on the S3, **never** rings an
alarm on the P4, and **never** shows a silently-wrong moon on the
knob.

Four facts carry the chain — all of them the code's own:

- **The boot order is the trust order** (``main.cpp``): WiFi first,
  then ``settings_store_load`` (:508 — *before* ``clock_init``, because
  the timezone comes from the store and must be set before any moon
  use, the comment in ``clock.c``'s epoch function says it plainly),
  then the boot NTP **synchronously inside ``clock_init``** (:512,
  timeout ``CLOCK_NTP_TIMEOUT_MS`` = 10 s), then the radio is dropped
  *before the panel ever wakes* — an associated modem's DTIM beacon
  steals QSPI DMA completions (task #29), so the boot sync is done and
  gone before the first flush.
- **Two plausibility floors, both parsed, never invented.** The NVS
  restore accepts only ``epoch >= 1609459200`` (pre-2021 is junk); a
  still-invalid system clock lands on the DEFAULT 2026-01-24 12:00
  (``clock_sntp.c``'s fallback chain). The knob's deep-sleep alarm gate
  uses its own floor — ``EPOCH_PLAUSIBLE_MIN`` 1600000000
  (``knob_power.c``) — and a restored-but-never-fixed instant arms the
  static moon through the **wall bool** ("a restore happened"), never
  through the raw clock's plausibility: the trust flag stays down until
  an SNTP fix, and "a factory default clock never shows a
  silently-wrong moon" is the code's own sentence for why.
- **One door to the sky** — ``clock_get_utc_epoch`` (``clock.c:257``):
  read the digits (an I2C transaction on the S3, ``time()`` on the
  P4/knob), run ``validate_time`` (year 2000..2099 plus the field
  ranges), reject implausible answers with ``ESP_ERR_INVALID_STATE``,
  else ``mktime`` the *local* digits into the UTC epoch under the TZ
  that ``clock_sntp_init`` set from the store. Five call sites in
  ``moon_ui.c`` (start, boot replay, the core-1 event scan, the day
  tick, the wake stale re-bake) each skip or defer on a reject —
  "Reject implausible dates so callers (moon phase) show nothing
  rather than a wrong phase" is the function's own comment, and the
  panel going black on a blank RTC is its visible half.
- **The flag gates what hurts to get wrong**: the P4's software alarm
  poll fires only while trusted (a default 2026-01-24 12:00 can never
  ring a phantom — ``clock_interrupt.c:183``), the P4's time label sits
  at ``LV_OPA_50`` until the flag flips (``screens.c:3813-3825``, compiled
  out on the S3), and the manual-entry commit (``screens.c:2924``) sets
  the time, the flag *and* the day tick together. ``clock_tz_apply``
  rewrites the S3's RTC local digits epoch-consistently (``mktime``
  **before** the env change) or re-derives the P4's, then re-arms the
  scheduler.

The whole chain, first sealed and then opened — three bases, the
fallback floors, the fixers, the epoch door and what the flag gates —
drawn from the code:

.. graphviz:: ../_graphs/time-chain.dot
   :alt: The chain of trust overview
   :align: center
   :caption: The overview — three bases feed one system clock, and only the fixers raise the trust flag; TZ comes from the store BEFORE any moon use (``clock_sntp.c:134-137``, the default Europe/Berlin = ``CLOCK_DEFAULT_TIMEZONE``). The moon walks exactly one door, ``clock_get_utc_epoch`` (``clock.c:257``): ``clock_get_time`` (I²C read on the S3, ``time()`` on the P4/knob) → ``validate_time`` (:271, year 2000..2099 + field ranges) → ``ESP_ERR_INVALID_STATE`` on any implausible answer → ``mktime`` reads the digits as LOCAL (TZ/DST) → UTC epoch — "show nothing rather than a wrong phase" is the reject's own comment. Every ``clock_get_utc_epoch`` site obeys it: ``moon_ui_start`` (``moon_ui.c:2790``) with no time leaves the panel BLACK ("a blank RTC leaves the moon black until a trustworthy time exists", ``main.cpp:675``), and boot replay's final leg, the core-1 event scan, the day tick and the wake stale re-bake each skip or defer on the reject — never a bake from the default 2026 date.

.. graphviz:: ../_graphs/time-chain-bases.dot
   :alt: The three time bases and the fallback chain
   :align: center
   :caption: The bases — the S3's PCF85063A sits on the shared BSP I²C at addr 0x51 (the RTC's OWN address, not the panel's 0x51 brightness command) with INT on TCA9554 EXIO3 (``clock.c:121/:128``); it holds LOCAL digits, battery-backed, self-trusting by design (the flag is compiled out of its consumers). The P4 runs the system clock over the internal RTC timer + SNTP (the curated pin is the external 32k crystal — see the bench note below) with the NVS ``clock``/``epoch`` snapshot saved on every SNTP fix and every 15 min from the minute tick (``CLOCK_NVS_SAVE_INTERVAL_SEC`` 900) — the only cross-power-loss memory once the crystal domain dies. The knob has no RTC: same NVS discipline plus a pre-sleep save on every deep-sleep entry (``knob_power.c:196``) and a one-time ADOPT + promote of the pre-seam ``knob_time`` namespace (``main.c:220-223``) — the restored instant is LAST KNOWN, never trusted. The fallback chain (``clock_sntp.c:219-243``): now < 1609459200 (pre-2021 = junk) → NVS restore (2021+ or nothing) → the DEFAULT 2026-01-24 12:00; both floors are parsed, not invented (the knob's rule gate uses its own 1600000000, ``knob_power.c:56``), and the fallback's ``settimeofday`` does NOT raise the trust flag.

.. graphviz:: ../_graphs/time-chain-trust.dot
   :alt: The trust raisers and what the flag gates
   :align: center
   :caption: The trust raisers — boot NTP is SYNCHRONOUS inside ``clock_init`` (``enable_ntp_sync`` at ``main.cpp:512``, timeout ``CLOCK_NTP_TIMEOUT_MS`` 10 s) and lands BEFORE the panel wakes (``wifi_manager_deinit`` follows at :541 — an associated modem's DTIM beacon steals QSPI DMA completions, task #29); the success path writes the S3's RTC and brings up home with a fresh clock. ``sntp_sync_time_cb`` (``clock_sntp.c:109``) raises the flag, force-snapshots the epoch (non-PCF builds) and releases the waiter; ``needs_sync()`` (``CONFIG_CLOCK_NTP_SYNC_INTERVAL_HOURS``, 1 h) re-arms the hourly gate. Manual entry exists for the P4 only (the hub's clock icon): the commit runs ``clock_set_time`` + ``clock_sntp_set_time_trusted(true)`` + ``moon_ui_day_tick`` (``screens.c:2924-2927``) — a user-entered time IS trusted, the rejected path logs "manual time … rejected". The flag-gated legs: the knob arms its static phase on the wall bool ("a restore happened") rather than raw plausibility, and the P4's ring needs ``clock_sntp_time_trusted()`` AND (the one-shot flag OR ``now >= next_epoch``) so the default 2026-01-24 can never phantom-ring (``clock_interrupt.c:183``).

One honest bench note on the P4's clock source. The curated
``sdkconfig.defaults.esp32p4`` pins ``CONFIG_RTC_CLK_SRC_EXT_CRYS=y``
("the internal RC oscillator is only a few % accurate and is unusable
as a clock time base", the defaults file's own comment — see
:doc:`the P4 board <../devices/p4>`), and ``-B build/p4`` treats the
**live** ``sdkconfig.p4`` as authoritative: a stale
``CONFIG_RTC_CLK_SRC_INT_RC=y`` pin in that gitignored file shadows the
default (a hand-edited live ``sdkconfig`` line beats every ``default``
line — CLAUDE.md's pin trap), which the generated
``build/p4/config/sdkconfig.h`` betrays. Grep the live file before
believing a built binary runs on the crystal; the documented remedy is
the deletion of the stale pin, not a doc edit.

The 90° orientation steps
-------------------------

**The short version.** The screen does not smoothly follow the
tilt. It waits until the device has clearly been *set down* at one
of four angles — 0, 90, 180, 270° — and then snaps there in one
atomic instant. Getting there is a funnel of four checks: look,
average away the jitters, pick the quadrant, and demand that the
answer persist before adopting it. A continuous rotation was
rejected on purpose: it would re-render the whole screen through
every intermediate angle, and every one of those in-between frames
is exactly the half-picture the atomic rule forbids.

The screen's orientation is not a continuous ramp — it is quantized
to **four 90° steps** (0/90/180/270° clockwise), applied as the LVGL
root's ``transform_rotation``. The pipeline (on the LVGL task, reading
the IMU itself once per 100 ms through a gate on the UI tick — its own
10 Hz, independent of the tilt monitor's 1 Hz timer):

1. **Sample** — 100 ms x/y gravity samples, int4-quantized.
2. **Window** — a 1 s windowed mean (10 samples) kills a single-sample
   spike; the mean must stay within one int4 level for ~1.5 s before
   it counts as **stable**.
3. **Quadrant** — the integer dot/cross of the stabilized vector with
   the anchor's home axis gives the candidate step; the step is
   applied **instantly** (one ``lv_refr_now`` — the atomic rule
   applies to a rotation as much as to a screen switch).
4. **Sustained confirmation** — a step candidate must persist
   (sustained across samples) before it is adopted, and re-tracking
   of a new home pose happens only when the step is 0 (the device
   flat, home orientation).

Read the funnel by what each stage refuses: *Sample* refuses to
trust a single reading; *Window* refuses a moving target — no
decision while the average is still wandering; *Quadrant* refuses
trigonometry, picking the quarter with dot/cross integers alone;
*Confirmation* refuses to flip on one stubborn sample. The pose
must survive all four before the screen moves — and when it does,
it moves once.

Why steps and not a ramp: a continuous ramp rotates the *whole
screen* (the LVGL root transform) through every intermediate angle,
which at 50 ms ticks costs a full-panel re-render per step — and a
partial rotation is exactly the half-frame failure mode the atomic
rule exists to forbid. Four discrete poses, each applied atomically,
is both cheaper and correct. (The direct-write spin had to pre-rotate
every frame by the current step and re-rotate the UI keep-mask with it
so its panel writes matched LVGL's root transform; that machinery was
removed 2026-11-13 — the root transform is now the only rotation the
panel sees (:doc:`the display pipeline <display>`).)

**The calibrations in one sentence each:** *home is where the desk
is* — the mount offset records which step the flat-on-desk pose
lands on, so "upright" means "as this housing sits on the desk",
whatever the panel's own 0° happens to be inside it; and *only the
flat pose may redefine home* — re-learning the home axis mid-flip
would remember a passing pose as permanent, which is the "the clock
woke up sideways" bug class.

Two calibration facts are load-bearing. The panel's **mount offset**
(Kconfig ``UI_ORI_BASE_V10``, per board: the P4 panel is mounted +90°
— step 1 — and the S3 AMOLED housing +180° — step 2, bench 2026-11-08)
puts the flat-on-desk pose at that step, so the step
machine's "home" is the physical desk pose, not the panel's 0°. And
a **180° flip** re-tracks the home axis only from step 0 — adopted
mid-flip, the flip's transient pose would become the new home (the
"the clock woke up sideways" bug class).

The idle/sleep state machine
----------------------------

**The short version.** Picture a three-rung ladder walked by two
timers. The device sits *awake* on the home screen. When the
activity timer expires — no touch, no sustained tilt, sixty seconds
by default — it steps down to *light sleep*: the moon freezes on
its current phase, the clock fades to 20 % opacity, and the whole
panel dims to 25 % with the panel's own brightness command, no
pixels rewritten; the dim *is* the sleep indicator. At that very
moment a one-shot countdown is armed, and when it fires the device
steps to *deep sleep*: the clock is hidden, the WiFi radio is
deinitialized, the backlight goes off — and only the RTC's crystal
keeps counting, because that is what lets the alarm ring later.
Every number here (the timeout, the countdown, the two dim
percentages) is a user setting.

The state machine lives in ``clock_idle.c`` (the activity timer) and
``actions.c`` (the transitions). Activity = a touch or a tilt
departure (below); the timer's timeout (``settings_idle_timeout_sec``,
default 60 s, user-tunable 60–1800 in settings) is the light-sleep
trigger.

.. figure:: ../img/screens/settings_light_sleep.png
   :alt: The idle-timeout settings screen
   :figclass: align-center

   The **idle timeout** setting — the wheel picks the seconds (the
   light-sleep trigger above), the X commits and returns.

**Light sleep** (``action_switch_to_light_sleep_screen``):

- the moon tick pauses on a **static frame**
  (:doc:`the animation pipelines <animations>`) — ``moon_ui_pause(true)``
  interrupts the orbit and parks the last baked viewport; the dusk
  pole spin that used to play here was retired 2026-11-13;
- the clock widget drops to ``UI_CLOCK_ALPHA_PCT`` (production:
  **20%** alpha) and the seconds timer pauses with the tick;
- the panel ramps to ``UI_LIGHT_SLEEP_DIM_PCT`` (production: **25%**)
  over the fade duration (``moon_ui_panel_dim_ramp`` — the same 0x51
  ramp as the screen-switch fade, serialized on ``s_panel_mtx``,
  0x51 ramps mutually exclusive via ``s_fade_busy``); the dim *is*
  the sleep transition;
- the deep-sleep countdown is armed **at the entry**
  (``clock_timer_enable(settings_deep_sleep_countdown_sec(), ...)``)
  — the RTC one-shot fires the countdown from the moment the device
  goes quiet (it used to wait for the dusk's last frame, so sleep
  depth followed the animation; there is no animation to wait for).

.. figure:: ../img/screens/light_sleep.png
   :alt: The light-sleep screen
   :figclass: align-center

   The **light-sleep** screen — the static current-phase moon, the
   panel ramped to 25 %, the clock at 20 % alpha still ticking the
   minutes (the seconds timer paused with the entry).

   On the real glass the 25 % is a *hardware* dim (the 0x51 ramp), so
   the whole panel — moon and digits — glows at a quarter of the home
   screen's brightness. The flat capture above cannot show that; the
   simulated composite can:

.. figure:: ../_static/photos/sim_light_sleep.png
   :alt: Simulated light-sleep render at 25 percent hardware brightness
   :figclass: align-center

   Light sleep on the Waveshare ESP32-S3-Touch-AMOLED-1.75 — the same
   frame at 25 % hardware brightness: the moon's lit side still reads
   as a moon, but the panel's black is truly black (AMOLED) and the
   whole image is a quarter as bright as the home screen.

**Deep sleep** (``action_switch_to_deep_sleep_screen``):

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

Everything that keeps a screen "live" is turned off — the tick, the
seconds timer, the clock update (the backlight gate), the WiFi radio.
The RTC's crystal clock keeps running, which is what the alarm wake
relies on.

.. figure:: ../img/screens/deep_sleep.png
   :alt: The deep-sleep screen
   :figclass: align-center

   The **deep-sleep** screen — the moon at its static phase under the
   crescent sleep glyph. This capture is the frame *just after* the
   entry: the code then hides the clock label and turns the backlight
   off, so on the panel the screen goes black right after this. The
   simulated composite shows the same last-lit frame on the glass:

.. figure:: ../_static/photos/sim_deep_sleep.png
   :alt: Simulated deep-sleep last-lit frame
   :figclass: align-center

   Deep sleep's last lit frame on the Waveshare ESP32-S3-Touch-AMOLED-1.75
   — the static moon and the crescent glyph; one backlight write later
   the panel is off until the next wake.

One S3/P4 asymmetry (the Waveshare ESP32-S3-Touch-AMOLED-1.75
versus the Waveshare ESP32-P4-WIFI6-Touch-LCD-XC): the P4 has no
PCF85063A ALM wake (the app-level
deep sleep does not call ``esp_deep_sleep`` — the crystal clock runs
through it), so on the P4 the device **stays in light sleep while an
alarm is pending** (dim panel, dark side) instead of dropping to the
backlight-off state. The user's power decision: an alarm must be
able to ring.

The third board runs a different ladder
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**The short version.** The knob shares the *look* of the ladder but
has none of the clocks' instruments: no IMU to feel a pick-up, no
RTC to arm, and — being battery-powered — the willingness to truly
switch itself off. It dims by LEDC duty instead of the panel
command and calls the real chip power-down. The alarm question is
answered by the chip's own timer: before sleeping, the knob arms a
wake-up for the next alarm rule, and on waking it recomputes that
rule from the time it last saved to flash — inside the 300 s grace,
show the silent alarm visual; within 900 s, hold at 1 Hz until the
rule arrives (**never ring early**); further off or implausible, it
was just a dial turn. Every wake re-corrects over the network, so
the timer's drift never compounds.

The knob (:doc:`the Knob board <../devices/knob>`) shares the
*screens* and the *scheduler* but not this state machine: no IMU to
wake on, no RTC to arm, no light sleep. Its
``knob_power`` supervisor dims by LEDC duty (``KNOB_DIM_DUTY`` 64 =
the clocks' 25 % panel rung) plus the shared clock widget at
``UI_CLOCK_ALPHA_PCT`` (20 %) — the same dimmed *look* as the clocks'
light sleep, two
rungs down from the clocks' ladder — and then calls the real
``esp_deep_sleep_start`` (the clocks' app-layer "deep sleep" never
powers down; the knob's does, battery honesty for a portable knob).
Since P2 the two rungs read the **same** ``settings.txt`` store pair
the clocks' sleep screens own (``idle_timeout_sec`` for the dim, one
``deep_sleep_countdown_sec`` later for the sleep — the clocks'
additive posture), so the shared sleep-screen rollers edit all three
boards' idle behaviour.
The alarm question the asymmetry above answers with *staying in light
sleep*, the knob answers with the **S3 internal RTC timer**: before
sleeping it arms ``esp_sleep_enable_timer_wakeup`` for the next fixed
rule alongside the EXT0 (dial-A) / EXT1 (tap) pads, and a cause-4
wake is arbitrated against the rule recomputed from the NVS epoch
snapshot — grace (300 s) → the silent alarm visual, early (≤ 900 s)
→ hold at 1 Hz until the rule (**never ring early**), further off or
implausible → a normal dial-style wake. RC-drift is the accepted
cost (~0.5 % worst case) and never compounds: every wake re-corrects
via SNTP and re-arms from the fresh instant. Full design:
:doc:`the knob display <knob-display>` — the ladder and the
arbitration.

The RTC alarm disarm rule
-------------------------

**The short version.** The RTC chip *remembers* its armed alarm
through a reboot — the register is battery-backed, not reset by
power. The scheduler rewrites that memory on every schedule, but a
device that boots with *no* alarm rules must go one step further
and erase it: otherwise the stale time left by some previous life
still fires, and a device with no alarms wakes at the wrong minute
showing a rule that no longer exists. That erase-on-zero-rules is
the invariant, and the headless device is the honest test — a
phantom wake never comes back to the desk.

The PCF85063A's alarm registers **persist across reboots** (they are
battery-backed, not reset by power) — the armed time, the AIE bit
AND the AF latch. The scheduler writes the next alarm time on every
schedule; but a boot with **zero alarm rules**
must explicitly ``clock_clear_alarm()`` — otherwise a stale armed
time from a previous boot fires a **phantom alarm**: the ALM pin
toggles, the device wakes from deep sleep at the wrong minute, and
shows the alarm screen for a rule that no longer exists. The disarm
on zero rules is the invariant; the headless device is the test that
proves it (a phantom wake never comes back to the desk).

Two legs of that invariant are easy to miss, and a user report
(2026-11-18, S3: a deactivated alarm still rang once) points at
them: the boot's ``init`` leg must run it even when
``alarm.txt`` is **missing** (``ESP_ERR_NOT_FOUND``) — a fresh flash
wipes the file, but the PCF's arm survives on the chip, so a
missing-file boot is a zero-rule boot too (``clock.c``'s init now
disarms there; until then it only warned). And the disarm must clear
the **AF flag itself**, not just AIE: AIE gates only the INT pin
output, the PCF still *latches* AF when the clock matches the ALM
registers with AIE=0, and the S3's 1300 ms poll reads the raw CTRL2
flags rather than the pin — so a latched AF surviving a settings
deactivation fires the alarm screen once on the first poll after it
(``clock_enable_alarm(false)`` now clears AF on the way out,
symmetric with the enable branch). The read side carried the same
asymmetry: the P4's poll branch fires only when the scheduler's cache
actually holds the next rule (``clock_alarm_get_next_scheduled``) and
the time is trusted, while the S3's AF branch fired on the raw flag —
so a stale latch rang with zero rules, and a hung LVGL task plus
watchdog reset re-fired it on every boot, pinning the device on the
alarm screen (2026-11-19, S3: stuck on the alarm screen with the
alarm disabled). The S3 branch is now gated on the same cache: a
latched AF with no scheduled rule is logged and cleared, never fired
(a legitimate deep-sleep ring is unaffected — boot loads the rules
and ``schedule_next`` fills the cache before the first poll).

The whole chain, first sealed and then opened at both ends — the rule
write, both boards' arms, the shared 1300 ms poll, and the action it
lands in — drawn from the code:

.. graphviz:: ../_graphs/alarm-lifecycle.dot
   :alt: The alarm lifecycle overview
   :align: center
   :caption: The overview — one scheduler writes both boards' arms (the S3's ALM pin, the P4's ``esp_timer`` one-shot) and both lands converge on ``action_alarm_triggered`` in the LVGL task; the next fire is re-armed by the poll, so a dismiss only ends the show.

.. graphviz:: ../_graphs/alarm-lifecycle-write.dot
   :alt: The alarm rule write
   :align: center
   :caption: The write — ``clock_alarm_set_pomodoro_interval`` (``*/N * * * *``), ``clock_alarm_set_clock_alarm`` (``M H * * dow``) and ``clock_alarm_deactivate_all`` ("# no alarms") all go through ``clock_alarm_scheduler_write``, which rewrites ``/spiffs/alarm.txt`` WHOLE — the file IS the rule set, reloaded on boot and armed from it (entries are mutually exclusive: exactly one rule kind exists). ``clock_alarm_schedule_next(current)`` is the single arming funnel, also run after every fire; the zero-rules branch runs ``clock_clear_alarm`` (AIE + ALM registers + the AF latch — a latched AF alone would still fire the poll) + ``disarm_esp_timer`` (the disarm invariant above; the boot runs the same leg when ``alarm.txt`` is missing), the ≥1 branch caches the next time and lands ``clock_set_alarm`` + ``clock_enable_alarm(true)`` — S3: PCF85063A ALM registers + AIE; P4: the same call is a validated no-op and the one-shot carries the fire. ``clock_trigger_alarm_update`` feeds the home row — the OFF half of the write path fires it too, so a deactivate-all shows ``--:--`` immediately.

.. graphviz:: ../_graphs/alarm-lifecycle-fire.dot
   :alt: The alarm fire paths and the shared poll
   :align: center
   :caption: The fires — on the S3 the ALM pin sets AF and drives TCA9554 INT to EXIO3 (``IO_EXPANDER_PIN_NUM_3``, the same line the deep-sleep wake rides); ``clock_handle_interrupts`` reads the interrupts and, GATED on the scheduler's cache (``clock_alarm_get_next_scheduled`` — the P4 parity: a latched AF with no rule scheduled is a stale latch, cleared without firing; 2026-11-19), triggers, clears AF and re-schedules. On the P4 the one-shot's callback only sets a flag on the esp_timer task (nothing UI there); the poll branch accepts flag OR wall-clock backup check (``now >= next`` catches a one-shot a time SET left stale), gated on the trust flag, then stops the one-shot and re-schedules. Both branches run inside the ONE shared poll — ``lv_timer_create(clock_rtc_interrupt_handler_cb, RTC_POLL_INTERVAL_MS = 1300)`` — whose ``clock_cb.c`` dispatchers call the registered actions DIRECTLY, in the LVGL task. The ring: backlight relight, ``g_alarm_active``, ``alarm_audio_ring_start`` (ES8311 gradual ramp; the knob is the codec-less silent board), ``g_asleep = true`` (dismissing it IS a wake event), tick → 10 ms, ``moon_ui_pause(true)`` (no baking for a screen that shows no moon), ``clock_timer_disable``, ``clock_idle_reset_timer`` (no auto-sleep while ringing), ``loadScreen(SCREEN_ID_ALARM)``. The dismiss: ``alarm_indev_read_cb`` fires on every press regardless of hit-test but only while ``g_alarm_active`` AND ``!clock_is_idle()`` — the asleep press is consumed by the earlier-registered idle-wake cb instead.

The interval (pomodoro) rule's settings-side half — the three-glyph
entry screen, the twin-roller screen whose RELEASED handler *is* the
commit, and the ``*/N`` midnight-anchored day-grid arithmetic — is
drawn in :doc:`the pomodoro interval <../ui/settings-pomodoro>`
(``pomodoro-cycle.dot``); the lifecycle above is where both rule
kinds land.

The tilt: activity and wake
---------------------------

**The short version.** The accelerometer feeds two decisions; this
page covers the second one: *is someone handling the device?* The
answer cannot be "the numbers moved" — a desk clock that wakes at
every footstep is a dead battery by morning. So the machine keeps a
slowly-following estimate of the pose the device has *settled*
into, and calls it activity only when the readings sustain a move
*away* from that pose: far enough, in a consistent direction, for a
couple of samples in a row. The pose it fell asleep at is the
reference; motion relative to it is the wake.

The tilt sensor (QMI8658C) drives three things — the
**activity/wake** signal, the whole-screen **90° orientation**
pipeline (above: the tilt's only UI coupling while the orbit is off),
and the moon's out-of-plane orbit
(:doc:`the animation pipelines <animations>`), which is
``UI_MOON_ORBIT_ENABLE`` default ``n`` because the ~2 s relief-DEM
re-bake cannot be served at the tilt cadence. The wake fires
on **departure from a settled pose**, not on raw per-sample deltas:

- the settled pose is tracked as an EMA of the gravity vector;
- a *departure* requires **two bars** — per-state thresholds on the
  same angle: ``UI_IMU_ACTIVITY_DEG`` (5°) while awake, the deliberate
  pick-up ``UI_IMU_WAKE_DEG`` (20°) while asleep — held for a
  **2-sample streak** before it counts as activity. The asleep band
  between the bars neither wakes nor holds.

The failure mode the design prevents is waking the device *right
after it slept*: a device on a desk jostled by a fan or a footstep
sits at a slightly different pose than the EMA it fell asleep at, and
a raw-delta wake fires the moment the sleep screen appears. The
departure model treats the settled pose as the *reference* and only
wakes on a sustained move *away from it* — the "the clock woke itself"
bug class, fixed at the signal level rather than with a sleep
debounce.

The full chain, first sealed and then opened per consumer — one read
path, two consumers on two clocks (the monitor's 1 Hz timer versus
the orientation pipeline's 100 ms gate on the UI tick), the settled
state as the single edge between them, and the 2 s press re-sync with
its stability and on-axis gates — drawn from the code:

.. graphviz:: ../_graphs/tilt-pipeline.dot
   :alt: The tilt chain overview
   :align: center
   :caption: The overview — the QMI8658C on the shared I²C (0x6A/0x6B) serves ONE burst-read path; ``imu_is_ready()`` gates every consumer, and the knob ships ``imu_stub.c`` (ready=false) AND excludes ``moon_ui.c`` entirely — its UI sits at the base orientation and tilt never wakes it. One sample is ``|g|``-banded: outside 0.5..1.5 g it is a shake or drop and gets DROPPED (a NACK-stuck bus logs the first and every 20th consecutive failure). The third consumer, the moon's out-of-plane orbit (``moon_imu_tick``, ``moon_ui.c:2303``, asin(−gz) → low-pass, dead-zone, clamp, quantize → re-bake), is OFF by default: ``UI_MOON_ORBIT_ENABLE`` is ``n`` because the ~2 s relief-DEM re-bake cannot be served at the tilt cadence on the S3 — orientation is the tilt's only UI coupling.

.. graphviz:: ../_graphs/tilt-pipeline-monitor.dot
   :alt: The 1 Hz activity/wake monitor
   :align: center
   :caption: The 1 Hz monitor — ``moon_tilt_monitor_cb`` runs its OWN 1 s ``lv_timer`` (``moon_ui.c:2729``), deliberately NOT the moon tick (which is PAUSED in sleep while the wake must run there; with ``CONFIG_UI_IMU_ENABLE=n`` the timer survives only to cache the LVGL task handle). A fast EMA (α 0.5) gives ``s_pose_g`` unit-normalized (the FIRST sample seeds both EMAs — zero departure); departure is ``acos(dot)`` between pose and settled reference in degrees, never a raw sample delta (the p90 ~3.3° at-rest noise floor is why raw deltas fired "woke right after sleep entry"). Below ``UI_IMU_ACTIVITY_DEG`` (5°) the pose is SETTLED and the slow EMA (α 0.2) absorbs it into the reference — only while settled, so the ~0.4–0.7°/s at-rest drift is tracked, not fought (the ceiling bar ×α/(1−α) = 1.25°/s; α 0.1 sat AT its ceiling on a hot board). The TWO BARS are per-state thresholds on the same departure: awake the 5° activity bar holds the idle timer; asleep the deliberate-pick-up bar ``UI_IMU_WAKE_DEG`` (20°) cannot be cleared by table vibration or drift — the asleep 5..20° band is neither. A streak of 2 consecutive qualifying samples gates both bars, and the 1 Hz call site is the ≤1/s rate cap on ``clock_idle_notify_tilt_activity`` (``clock_idle.c:92``), which always resets the idle stamp and, when asleep, opens the tilt door of ``wake-doors.dot``.

.. graphviz:: ../_graphs/tilt-pipeline-orient.dot
   :alt: The 10 Hz orientation pipeline
   :align: center
   :caption: The orientation pipeline — ``screen_orientation_apply`` runs every 10 ms UI tick but samples only through the 100 ms gate (``ORI_SAMPLE_MS``) → 10 Hz; the monitor's 1 Hz timer does not feed it. x/y are int4-quantized (``ORI_Q_SCALE`` 8, one level = 1/8 g, clamped ±1 g) and a flat gate below 0.25 g leaves the in-plane pose UNDEFINED — orientation, window and reference frozen. The 1 s running mean (``ORI_WINDOW`` 10) is the 1 Hz low-pass; STABLE means the filtered mean holds within ONE int4 level for ``ORI_STABLE_SAMPLES`` 15 (~1.5 s). An integer dot/cross quadrant decides step ∈ {0, 90, 180, 270}° CW-positive with no atan2 and no floats, decided BEFORE the reference re-tracks — and re-tracking happens only while SETTLED and step 0, so a fast 180° flip cannot adopt its transient pose as home (the "clock woke up sideways" bug class). The step must be produced by 10 consecutive samples (~1 s) before it applies; a deliberate move costs at most ~1 s. ``v10 = (ORI_BASE_V10 + s_user_base_v10) − step×10`` (sign via ``UI_CLOCK_ROLL_SIGN``; base 900 = +90° on ALL THREE panels — the single shared panel-mount offset, verified on the bench 2026-11-16 — and ``s_user_base_v10`` the persisted wall-mount trim, ``display_rotation_deg`` ×10, seeded at boot by ``action_orientation_init`` and moved live by the Settings display roller), and ``ori_apply_v10`` pushes LVGL ``transform_rotation`` to ALL 17 screen roots, pivot centre — the rotated root's panel-sized RGB565 layer (~0.4 MB @466, ~1.0 MB @720) comes from the CLIB malloc → PSRAM (the 64 KB LVGL pool cannot hold it); hit test inverse-maps through the transform, step 0 draws direct at zero cost. The 2 s press re-sync (``action_register_orientation_touch``) is ACTIONED only if stable AND on-axis (``|dot|² ≥ 3|cross|²`` — the 2026-10-30 "flipping does not reliably work" fix: a palm released mid-flip is dropped), and it re-anchors home and pushes the base orientation.

The sleep ladder: awake → light → deep
--------------------------------------

**The short version.** Sleep is a ladder with two rungs and an
independent countdown per rung. The idle timeout
(``settings_idle_timeout_sec``, ``CLOCK_IDLE_TIMEOUT_SEC`` default
60 s) drops the awake device onto the light-sleep screen — radio
down, UI tick slowed to ``UI_LIGHT_SLEEP_TICK_MS`` (100 ms), the
clock container at ``UI_CLOCK_ALPHA_PCT`` (20 %) over the 0x51 panel
ramp to ``UI_LIGHT_SLEEP_DIM_PCT`` (25 %). The light-sleep **entry** is what
arms the deep-sleep countdown (``settings_deep_sleep_countdown_sec``,
``UI_DEEP_SLEEP_COUNTDOWN_SEC`` default 60 s) — sleep depth follows
the RTC timer, never an animation (the retired dusk spin's ladder
survived as exactly that line). The deep rung has two guards: demo
mode refuses the transition and disarms the source, and the P4 with
a pending alarm **stays on the lighter rung** (its deep state is
app-level — no ``esp_deep_sleep`` — so the software one-shot could
otherwise ring from the dark; the S3 needs no such guard, its
PCF85063A ALM pin wakes the deep state for real). The knob runs the
same ladder with hardware rungs: LEDC backlight dim (GPIO 47) for
light sleep, ``esp_deep_sleep_start`` with an RTC TIMER wake for the
deep one (:doc:`knob display <knob-display>`).

The entry order, the arm-at-entry, both guards and the one wake tail
that climbs back — first sealed, then the guard ladder opened — drawn
from the code:

.. graphviz:: ../_graphs/sleep-entry.dot
   :alt: The sleep-entry ladder overview
   :align: center
   :caption: The ladder — the idle timeout fires ``action_switch_to_light_sleep_screen`` (``main.cpp:625`` registers it; the period is ``settings_idle_timeout_sec``, ``CLOCK_IDLE_TIMEOUT_SEC``, default 60 s, 60..1800, reset by any tap or tilt). The light entry: ``g_asleep`` true, ``wifi_manager_deinit`` (the radio DOWN — the airplane half of the name), UI tick 10 ms → ``UI_LIGHT_SLEEP_TICK_MS`` (100), seconds timer PAUSED, idle monitor OFF, ``moon_ui_pause(true)`` (moon static) — and THEN the deep countdown is armed (``clock_timer_enable(settings_deep_sleep_countdown_sec, 1 Hz)`` AT ENTRY): the retired dusk spin's ladder survived as exactly this, sleep depth follows the clock, never an animation. The light screen shows the clock container at ``UI_CLOCK_ALPHA_PCT`` (20 %) over the 0x51 ramp to ``UI_LIGHT_SLEEP_DIM_PCT`` (25 %), the RTC countdown ticking in the PCF85063A timer (S3) / ``esp_timer`` (P4). The deep entry hides the clock container (``LV_OBJ_FLAG_HIDDEN``, re-applied EVERY entry so a wake-to-other-screens can't leak visible digits), pauses the moon, disables the spent countdown and sets ``moon_ui_panel_brightness_set(0)`` — the backlight is OFF (``g_backlight_off``), and the RTC clock update is gated on it.

.. graphviz:: ../_graphs/sleep-entry-guards.dot
   :alt: The sleep-entry guard ladder
   :align: center
   :caption: The guards — ``main.cpp:629`` dispatches ``action_switch_to_deep_sleep_screen`` on the LVGL task when ``settings_deep_sleep_countdown_sec`` (``UI_DEEP_SLEEP_COUNTDOWN_SEC``, default 60 s, 60..1800 — the settings-deep-sleep picker writes it) expires; demo mode refuses the transition AND calls ``clock_timer_disable`` (a dead-black panel mid-recording would ruin the take), and the P4 with a pending alarm returns to the light screen instead — its deep state is app-level (the crystal keeps running), so the software one-shot could otherwise ring from the dark, and the user's power decision keeps the device in the lighter state with the one-shot still armed.

The wake paths
--------------

**The short version.** Three doors open the same way — a finger (a
tap), a hand (a sustained tilt away from the settled pose), and the
clock itself (the alarm) — and all three run one tail: stop any
ring, relight the panel if it was off, and head for the airplane
screen. The one deliberate split is the time check: the WiFi
reconnect and SNTP run on their own background task, never on the
UI worker, so the screen you just woke up to can repaint while the
radio works — that split is the whole story of the 1.7 s wake
freeze. The moon comes back at its current phase *immediately*,
with no story or replay; if the stored frame has gone stale, the
replacement bakes in the background and lands atomically when done,
for exactly the same reason.

Three wakes, one handler (``action_wake_screen``):

1. **Touch** — the wake tap (a tap on a sleep screen's root; the
   deliberate second tap opens the settings hub — the wake tap itself
   fires before the screen swaps).
2. **Tilt departure** — the idle-wake callback from the tilt monitor.
3. **Alarm** — the ALM pin (S3: a real deep-sleep wake; P4: the
   software one-shot from light sleep).

The guards are per-door — the idle-state test on the tap, the
departure bars + streak on the tilt, the ``g_alarm_active`` /
awake-bypass on the ring — and the tail is one handler. The awake
alarm dismiss is the sketch's one edge that deliberately skips the
``clock_trigger_idle_wake`` funnel (a tap while awake otherwise only
resets the idle timer, which is exactly the one-way door the
callback's comment documents). How the finger and the dial get to
these doors in the first place — the one BSP-owned indev with its
three co-located listeners and the knob's own polled read paths — is
:ref:`input-chain-ui <input-chain-ui>`. The sketch is the overview
plus the two opened ends:

.. graphviz:: ../_graphs/wake-doors.dot
   :alt: The wake doors overview
   :align: center
   :caption: The overview — the three doors funnel through ``clock_trigger_idle_wake`` (``clock_cb.c``; ``main.cpp`` registers the idle-wake callback as ``action_wake_screen`` via ``clock_register_idle_wake_callback``); only the alarm-screen dismiss skips the funnel and lands on the tail directly.

.. graphviz:: ../_graphs/wake-doors-doors.dot
   :alt: The per-door wake guards
   :align: center
   :caption: The guards — the touch door is ``indev_read_cb`` (LV_EVENT_PRESSED/PRESSING, registered by ``clock_idle_init``, ``clock_idle.c:70/:71``): PRESSED while IDLE triggers the wake, while AWAKE the same tap only RESETS the idle timer — that asymmetry is the one-way door. The tilt door wakes only on DEPARTURE from the settled-pose EMA (two per-state bars — 5° activity awake, 20° ``UI_IMU_WAKE_DEG`` asleep — held for a 2-sample streak, ≤1/s from the 1 Hz call site); raw deltas are the "the clock woke itself" bug class, and the full chain is ``tilt-pipeline.dot``. The alarm door: ``action_alarm_triggered`` sets ``g_asleep = true`` (the ring screen counts as asleep), and while AWAKE at that screen the idle door cannot fire — ``alarm_indev_read_cb`` is the second indev cb that dismisses anyway (``g_alarm_active && !clock_is_idle()`` → ``action_wake_screen`` on the same press).

.. graphviz:: ../_graphs/wake-doors-tail.dot
   :alt: The shared wake tail
   :align: center
   :caption: The tail — ``action_wake_screen`` (``actions.c``, LVGL task) stops the ring FIRST (every dismiss path runs through here), relights the backlight if ``g_backlight_off``, restores the clock alpha, CONSUMES ``g_asleep`` (the stale-alarm branch: was_asleep && WiFi off → a normal wake, never hijacked), resumes the 10 ms tick and seconds timer, disables the countdown, un-pauses the moon and runs ``moon_ui_day_tick`` (re-bake only if the day rolled over), then lands the airplane screen OR the on-demand sync — the radio off the UI task via a background task (stack 6144) running ``wifi_manager_init`` + SNTP, with the UI tick upgrading the WiFi screen to airplane on the done flag (the 1.7 s wake-freeze fix; ``wifi-sync.dot`` owns the starters). The idle re-arm runs only when ``!moon_ui_replaying()`` — a touch during the boot presentation wakes the SCREEN, and the replay's tick end leg re-arms the ladder when the animation is over.

.. figure:: ../img/screens/alarm.png
   :alt: The alarm screen
   :figclass: align-center

   The **alarm** screen — the third wake, shown while the ring plays
   (a touch stops the ring and heads home). The weekday arc above the clock
   marks the rule's active days. On the glass (this is the only wake
   that matters to the dark panel — the alarm rings on a backlight
   that was off):

.. figure:: ../_static/photos/sim_alarm.png
   :alt: Simulated alarm screen on the panel
   :figclass: align-center

   The alarm wake on the Waveshare ESP32-S3-Touch-AMOLED-1.75 — the
   minute and the alarm glyph at full brightness, straight out of
   deep sleep.

All three run the same tail:

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

The sync runs on a **background task** (stack 6144) — ``wifi_manager_init``
plus SNTP — off the LVGL task, and the *screen upgrade* from the WiFi
screen back to airplane happens from the UI tick when the done flag
lands. That split is the fix for the **1.7 s wake freeze**: the
original wake called the synchronous WiFi reconnect *on the LVGL
task*, and the LVGL task — the task that repaints the wake screen —
blocked for the whole reconnect. The freeze was not a crash and not a
WDT; it was the UI's own task parked inside the radio stack.

The wake shows the **current phase static** (:doc:`the animation
pipelines <animations>`) — no sweep at wake, by design: the sweep
(retired 2026-11-13) was the boot story ("here is how the moon got to
where it is"), and a wake is an answer, not a story. The frame the wake displays is the
boot's bake when the day has not turned; when it *is* stale (new
day, new event tint, mid-bake at sleep) the wake still returns
immediately — the re-bake runs on a core-1 task and the next tick
displays the finished frame atomically, exactly as the 1.7 s
WiFi-freeze fix moved the radio
off this task.

The hourly sync
---------------

**The short version.** Once an hour the device double-checks its
clock against the network — but only if it is *already* awake and
sitting still on the home screen. A sleeping device is never woken
for it (its next real wake syncs anyway), an open settings session
outranks it, and the WiFi screen is a weather vane: up while the
sync is in flight, down the moment it lands, whether it succeeded
or failed.

While the device is **at rest on the airplane screen**, at the top of
a minute, when it is not connected and the RTC is due for a refresh
(``CLOCK_NTP_SYNC_INTERVAL_HOURS``, production: 1 h), the same sync
path fires. The policy is deliberately conservative:

- **at rest only** — a sleeping device is not woken (the power net
  loss; the next wake re-syncs);
- **no interference** — a settings session and an active sync gate
  the trigger off (the sweep's gate entry is gone with the sweeps —
  clocks 2026-11-13, knob 2026-11-14);
- **transient** — the WiFi screen shows only while the sync is
  in flight; success or failure both drop back to airplane.

The state machine, in full
---------------------------

**The short version.** Read the diagram as the prose: the left side
is the awake life (boot → sync → airplane ↔ hub on a tap), the
right side is the ladder (airplane → dim → dark), and the arrows
climbing back up are exactly the three doors. Every labelled
transition — the 60 s idle, the 60 s countdown
(``UI_DEEP_SLEEP_COUNTDOWN_SEC``, min 60), the two wake
routes, and the P4's refusal to drop into the dark while an alarm
is pending — is a section of this page.

.. raw:: html

   <pre class="mermaid">
   stateDiagram-v2
     [*] --> BOOT
     BOOT --> WIFI: NTP ok — static-phase bake (the 1344-frame boot sweep retired 2026-11-13)
     BOOT --> AIRPLANE: NTP fail
     WIFI --> AIRPLANE: sync complete
     AIRPLANE --> HUB: tap — the sub-screens (exit returns with no sync kicked off)
     HUB --> AIRPLANE
     AIRPLANE --> LIGHT_SLEEP: idle 60 s — static dim (25% panel, 20% clock alpha, countdown armed at entry)
     LIGHT_SLEEP --> AIRPLANE: wake (touch / tilt / alarm) — backlight → 100, static phase, on-demand sync
     LIGHT_SLEEP --> DEEP_SLEEP: RTC countdown (60 s default)
     DEEP_SLEEP --> LIGHT_SLEEP: ALM pending (P4 stays dim, does not drop out)
     DEEP_SLEEP --> AIRPLANE: wake — ALM pin (S3) / software one-shot (P4), or tilt
     DEEP_SLEEP --> DEEP_SLEEP: touch — none on the S3
   </pre>

The deep-sleep entry (the right-hand column above) does the rest:
WiFi deinit, the clock label hidden, the backlight off, the tick and
seconds timers paused.

Every transition is one atomic flush (the invariant), every panel
brightness write is the serialized 0x51 path (:doc:`the display pipeline <display>`), and every
animation is display-paced off a once-built map (:doc:`the animation pipelines <animations>`). The
remaining page of this section is the P4 port — how the same machine
runs on a different panel, a different bus, and a remote Wi-Fi core:
:doc:`The P4 port <p4-port>`.
