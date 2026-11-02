Chapter 15 — The clock and the sleep machine
============================================

The moon pipelines decide what to show; this chapter's machine decides
*when* — the minute-driven clock, the 90° orientation steps, the
idle/sleep state machine (light sleep = the dimmed dusk, deep sleep =
backlight off), and the three wake paths (touch, tilt, alarm). It is
the part of the product where a *clock* and a *moon* have to agree on
every minute of the day.

The screen model
----------------

The home screen is the **airplane (modem-sleep) screen**: the moon in
the foreground, the clock label, the seconds arc — no icons. The
**WiFi screen** is the *same* moon image plus a transient indicator,
shown only while a sync is in flight (boot, hourly, manual), then
dropped back to airplane. Because both screens carry the same moon
image, the switch between them is the *both-moon* case of the fade
rule (Chapter 14): no dim, one atomic whole-panel flush, and the
spin/sweep animation flows seamlessly across the switch.

The **settings hub** (tap on the home or WiFi screen) and its
sub-screens (alarm, pomodoro, alarm time, light-sleep, deep-sleep,
timezone, WiFi) carry *no* moon image. ``moon_screen_has_image()``
checks the active screen's root against the moon images' roots — and
that check is the lever that **starts and stops the spin render
task**: on a moon-less screen the spin and the tick's full-panel
``lv_refr_now`` would idle at ~50% of one core redrawing nothing.

The minute-driven clock
-----------------------

The clock display updates are **minute-driven, on the LVGL task**:
``action_update_clock_display`` fires when the displayed minute
changes, writes the clock label, and refreshes the seconds arc. Two
gates matter:

- **The backlight gate** — while the panel is off (deep sleep), the
  clock update is skipped entirely (``g_backlight_off``): the RTC
  keeps running (the crystal clock runs through the app-level deep
  sleep), the screen is dark, and the next wake restores the panel
  and the minute update re-applies.
- **The sweep gate** — an on-demand WiFi sync (below) may not fire
  while the boot sweep is playing: it would switch screens mid-sweep.

The 90° orientation steps
-------------------------

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

Why steps and not a ramp: a continuous ramp rotates the *whole
screen* (the LVGL root transform) through every intermediate angle,
which at 50 ms ticks costs a full-panel re-render per step — and a
partial rotation is exactly the half-frame failure mode the atomic
rule exists to forbid. Four discrete poses, each applied atomically,
is both cheaper and correct. The spin's direct write knows about the
steps (``s_panel_step``): its ``spin_step_rotate`` pre-rotates every
frame by the current step so the panel shows the same pixels LVGL's
own transform would, and the UI keep-mask re-rotates with the step
(Chapter 13).

Two calibration facts are load-bearing. The panel's **mount offset**
puts the flat-on-desk pose at step 1 (the base +90°), so the step
machine's "home" is the physical desk pose, not the panel's 0°. And
a **180° flip** re-tracks the home axis only from step 0 — adopted
mid-flip, the flip's transient pose would become the new home (the
"the clock woke up sideways" bug class).

The idle/sleep state machine
----------------------------

The state machine lives in ``clock_idle.c`` (the activity timer) and
``actions.c`` (the transitions). Activity = a touch or a tilt
departure (below); the timer's timeout (``settings_idle_timeout_sec``,
default 60 s, user-tunable 10–300 in settings) is the light-sleep
trigger.

**Light sleep** (``action_switch_to_light_sleep_screen``):

- the moon **dusk** plays (Chapter 14) — the constant-projection pole
  spin to the dark side;
- the panel ramps to ``UI_LIGHT_SLEEP_DIM_PCT`` (production: **25%**)
  over the fade duration (``moon_ui_panel_dim_ramp`` — the same 0x51
  ramp as the screen-switch fade, serialized on ``s_panel_mtx``,
  spin writes held by ``s_fade_busy``);
- the deep-sleep countdown is armed **on the dusk's last frame**
  (``clock_timer_enable(settings_deep_sleep_countdown_sec(), ...)``)
  — the RTC one-shot fires only once the fully unilluminated side is
  on screen, so the sleep depth follows the animation, not a timer
  started at the entry.

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

One S3/P4 asymmetry: the P4 has no PCF85063A ALM wake (the app-level
deep sleep does not call ``esp_deep_sleep`` — the crystal clock runs
through it), so on the P4 the device **stays in light sleep while an
alarm is pending** (dim panel, dark side) instead of dropping to the
backlight-off state. The user's power decision: an alarm must be
able to ring.

The RTC alarm disarm rule
-------------------------

The PCF85063A's alarm register **persists across reboots** (it is
battery-backed, not reset by power). The scheduler writes the next
alarm time on every schedule; but a boot with **zero alarm rules**
must explicitly ``clock_clear_alarm()`` — otherwise a stale armed
time from a previous boot fires a **phantom alarm**: the ALM pin
toggles, the device wakes from deep sleep at the wrong minute, and
shows the alarm screen for a rule that no longer exists. The disarm
on zero rules is the invariant; the headless device is the test that
proves it (a phantom wake never comes back to the desk).

The tilt: activity and wake
---------------------------

The tilt sensor (QMI8658C) drives two things — the orbit's sustained
poses (Chapter 14) and the **activity/wake** signal. The wake fires
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

Three wakes, one handler (``action_wake_screen``):

1. **Touch** — the wake tap (a tap on a sleep screen's root; the
   deliberate second tap opens the settings hub — the wake tap itself
   fires before the screen swaps).
2. **Tilt departure** — the idle-wake callback from the tilt monitor.
3. **Alarm** — the ALM pin (S3: a real deep-sleep wake; P4: the
   software one-shot from light sleep).

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

The wake shows the **current phase static** (the pre-baked static
frame, Chapter 14) — no sweep at wake, by design: the sweep is the
boot story ("here is how the moon got to where it is"), and a wake
is an answer, not a story.

The hourly sync
---------------

While the device is **at rest on the airplane screen**, at the top of
a minute, when it is not connected and the RTC is due for a refresh
(``CLOCK_NTP_SYNC_INTERVAL_HOURS``, production: 1 h), the same sync
path fires. The policy is deliberately conservative:

- **at rest only** — a sleeping device is not woken (the power net
  loss; the next wake re-syncs);
- **no interference** — the sweep, a settings session, and an active
  sync all gate the trigger off;
- **transient** — the WiFi screen shows only while the sync is
  in flight; success or failure both drop back to airplane.

The state machine, in full
---------------------------

::

   BOOT
     └─ (NTP ok) WIFI screen ←── boot sweep (336 f, ~2.8 min)
          └─ sync complete ──► AIRPLANE (home) ←──┐
     └─ (NTP fail) AIRPLANE                      │
                                                 │  tap → HUB → sub-screens
   AIRPLANE ── idle 60 s ──► LIGHT SLEEP ──┐     │   (hub exit → sync → AIRPLANE)
     ▲                                     │     │
     │ wake (touch / tilt / alarm)         │     │
     │  • backlight → 100                 │ dusk │
     │  • static phase + spin resume      │ (pole│
     │  • on-demand sync (WiFi screen)    │  spin│
     └─────────────────────────────────────┤  to  │
                                           │  dark│
                                           │  side│
                                           │  + 25%│
                                           │  dim) │
                                           └─ RTC countdown (30 s) ─► DEEP SLEEP
                                                                         • WiFi deinit
                                                                         • clock hidden
                                                                         • backlight OFF
                                                                         • ticks paused
        DEEP SLEEP ── ALM pin (S3) / software one-shot (P4) ──► wake path
        DEEP SLEEP ── touch (S3: no) / tilt ──► wake path

Every transition is one atomic flush (the invariant), every panel
brightness write is the serialized 0x51 path (Chapter 13), and every
animation is display-paced off a once-built map (Chapter 14). The
remaining chapter of this part is the P4 port — how the same machine
runs on a different panel, a different bus, and a remote Wi-Fi core:
Chapter 16.
