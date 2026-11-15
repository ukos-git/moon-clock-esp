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
  away* from the settled pose, not on raw motion: two thresholds
  (magnitude and direction) held for two consecutive samples.
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
   :alt: The timezone / UTC-offset settings screen
   :figclass: align-center

   The **timezone / UTC offset** screen — one of 53 thirty-minute
   offsets (Europe/Berlin here); committing rewrites the RTC
   epoch-consistently and re-arms the alarm (:doc:`the architecture <architecture>`,
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
changes, writes the clock label, and refreshes the seconds arc. Two
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
root's ``transform_rotation``. The pipeline (on the LVGL task, fed by
the tilt monitor's 1 Hz samples):

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

The PCF85063A's alarm register **persists across reboots** (it is
battery-backed, not reset by power). The scheduler writes the next
alarm time on every schedule; but a boot with **zero alarm rules**
must explicitly ``clock_clear_alarm()`` — otherwise a stale armed
time from a previous boot fires a **phantom alarm**: the ALM pin
toggles, the device wakes from deep sleep at the wrong minute, and
shows the alarm screen for a rule that no longer exists. The disarm
on zero rules is the invariant; the headless device is the test that
proves it (a phantom wake never comes back to the desk).

The whole chain — the rule write, both boards' arms, the shared
1300 ms poll, and the action it lands in — drawn from the code:

.. graphviz:: ../_graphs/alarm-lifecycle.dot

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

The tilt sensor (QMI8658C) drives two things — the orbit's sustained
poses (:doc:`the animation pipelines <animations>`) and the **activity/wake** signal. The wake fires
on **departure from a settled pose**, not on raw per-sample deltas:

- the settled pose is tracked as an EMA of the gravity vector;
- a *departure* requires **two bars** (magnitude and direction
  thresholds) held for a **2-sample streak** before it counts as
  activity.

The failure mode the design prevents is waking the device *right
after it slept*: a device on a desk jostled by a fan or a footstep
sits at a slightly different pose than the EMA it fell asleep at, and
a raw-delta wake fires the moment the sleep screen appears. The
departure model treats the settled pose as the *reference* and only
wakes on a sustained move *away from it* — the "the clock woke itself"
bug class, fixed at the signal level rather than with a sleep
debounce.

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
callback's comment documents):

.. graphviz:: ../_graphs/wake-doors.dot

.. figure:: ../img/screens/alarm.png
   :alt: The alarm screen
   :figclass: align-center

   The **alarm** screen — the third wake, shown while the ring plays
   (a touch is the snooze/dismiss). The weekday arc above the clock
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
