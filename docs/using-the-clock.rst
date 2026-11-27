Using the clock
===============

.. brief:
   Reader: someone holding a finished clock.
   They already know: nothing about the firmware.
   After this page they can: read the home screen, change settings, set an alarm, connect Wi-Fi.
   Page type: user guide (how-to).

This page explains how to operate a finished clock: what the home
screen shows, how to reach the settings, and what each setting does.
For every screen there is also a detailed page in :doc:`the screen
reference <ui/index>`, with annotated screenshots for all three
boards.

The clock has no buttons. Everything works with a tap, a drag on a
scroll wheel, or (on the knob board) a turn of the dial.

The home screen
---------------

.. figure:: _static/photos/sim_home.png
   :alt: The home screen in the round case: the Moon, the time in large digits, the next alarm time below it, and a thin seconds ring at the edge
   :width: 60%
   :align: center

The home screen shows:

- **the Moon** as it looks now, from the city of your time zone. It is
  redrawn once an hour;
- **the time** in hours and minutes;
- **the next alarm** below the time, for example ``7:05``. If the alarm
  is more than a day away, a weekday comes first (``Su 20:00``). If no
  alarm is set it shows ``--:--``;
- **a thin ring at the edge** that fills up with the seconds. While the
  clock is drawing a new Moon, the same ring shows the progress.

Tap anywhere on the home screen to open the settings.

Settings
--------

.. figure:: img/screens/settings_hub.png
   :alt: The settings hub: a white close button in the centre and a ring of seven icons around it
   :width: 50%
   :align: center

   The settings hub. Tap an icon to open that setting; tap the centre
   button to go back.

The settings screen is a ring of icons around a close button. Tap an
icon to open its screen. Every setting screen has the same close
button (a cross in a circle), which takes you back one level.

Most settings use a scroll wheel. Drag it up or down until the value
you want sits in the middle, then lift your finger: the clock saves the
value when you let go.

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Icon
     - What it sets
   * - Speaker
     - :doc:`The alarm sound <ui/settings-alarm-sound>`. Choose one of
       four sounds; the clock plays a short preview.
   * - Alarm clock
     - :doc:`The alarm <ui/settings-alarm>` (see below).
   * - Outlined moon
     - :doc:`When the clock dims <ui/settings-light-sleep>`: 1 to 30
       minutes without touch or movement.
   * - Solid moon
     - :doc:`When the clock goes dark <ui/settings-deep-sleep>`: 1 to 30
       minutes after it has dimmed.
   * - Wi-Fi
     - :doc:`The network <ui/settings-wifi>` (see below).
   * - Globe
     - :doc:`The time zone <ui/settings-tz>`. The list has 67 named
       zones, such as ``Europe/Berlin``. The zone sets both the time and
       the way the Moon is tilted.
   * - Rotating arrow
     - :doc:`The mounting angle <ui/settings-display>`. If you hang the
       clock on a wall at a slant, turn this until the screen is level.
       The screen tilts as you go, so you can judge it by eye.
   * - Clock (4C board only)
     - :doc:`Set the time by hand <ui/settings-time>`, for use without
       Wi-Fi.

Connecting to Wi-Fi
-------------------

The clock needs Wi-Fi once to learn the time; after that it checks in
about once an hour and keeps its radio off the rest of the time.

1. Open the settings and tap the **Wi-Fi** icon. The clock searches for
   networks and lists the ones it finds.
2. Scroll the list until your network is in the middle, then tap it.
   A round keyboard opens.
3. Type the password (see below).
4. Tap the **check mark** at the bottom. The clock saves the password,
   connects, and sets its time. You see a short Wi-Fi screen while this
   happens, then the home screen.

The password is stored on the clock and survives a power cut
(:doc:`the settings store <reference/spiffs-store>`).

.. figure:: _static/photos/sim_keyboard.png
   :alt: The round keyboard: letters arranged around the edge in twelve groups
   :width: 50%
   :align: center

   The round keyboard.

**Typing on the round keyboard.** The letters sit around the edge in
twelve groups. Press a group and only its letters appear, larger.
Slide your finger to the letter you want and lift it: that letter is
added. Nothing is typed until you lift your finger, so a slow slide
never adds a letter twice.

- The arc at the top switches between lower case, upper case, digits
  and symbols.
- A tap in the centre deletes the last character.
- The **cross** at the bottom clears what you typed; on an empty entry
  it leaves the keyboard. Holding the centre does the same.

:doc:`The round keyboard <keyboard>` explains the design, and
:doc:`the keyboard lab <keyboard-lab>` compares it with twelve other
designs.

Setting an alarm
----------------

Tap the **alarm clock** icon in the settings. The alarm screen has three
round buttons:

- the **large one in the middle** switches all alarms on or off. White
  means an alarm is set; grey means nothing will ring;
- the **one on the right** opens the daily alarm. Set the time (in
  5-minute steps) on the two scroll wheels, and drag the two handles
  on the arc at the top to choose the days, for example Monday to
  Friday;
- the **one on the left** opens the interval timer, which rings every
  so many hours and minutes (a "pomodoro" timer).

You can use either the daily alarm or the interval timer, not both:
turning one on replaces the other.

When the alarm rings, touch the screen anywhere to stop it. If nobody
does, it stops by itself after 30 minutes.

Sleep and waking up
-------------------

The clock saves power and screen life by sleeping in two stages.

.. list-table::
   :header-rows: 1
   :widths: 20 40 40

   * - Stage
     - What you see
     - When it starts
   * - Awake
     - Full brightness.
     - After any touch or movement.
   * - Dimmed
     - The screen drops to a quarter of its brightness and the time
       fades. The Moon stays.
     - After 1 minute without touch or movement (adjustable, 1–30 min).
   * - Dark
     - The screen goes black.
     - 1 minute after dimming (adjustable, 1–30 min).

A touch wakes the clock from either stage. On the two boards with a
motion sensor, tilting or picking up the clock wakes it too. An alarm
always wakes it.

The first touch only wakes the clock; it does not open the settings.
Tap again once the screen is bright.

To keep the clock awake all the time, open the dim setting and tap the
**power icon** next to the scroll wheel so it turns grey. To let it dim
but never go dark, do the same on the dark setting.

The knob board
--------------

The knob board has a dial around its screen:

- On the home screen, turning the dial changes the brightness.
- In the settings, the dial moves a highlight from icon to icon, or
  turns the scroll wheel on a setting screen. Tap to confirm.
- It runs on a battery and shows the charge level on screen.
- Instead of dimming the picture, it dims its backlight; instead of a
  black screen, it switches itself off completely. Turning the dial
  wakes it, and it wakes by itself in time for an alarm.

:doc:`The knob board <devices/knob>` has the details.

If something goes wrong
-----------------------

The time is wrong
   Check the time zone in the settings. If the clock has never been on
   Wi-Fi, connect it once (see above).

The clock does not find my network
   The clock uses 2.4 GHz Wi-Fi. Leave the Wi-Fi screen and open it
   again to search once more.

The screen stays dark
   Touch it. If it still stays dark, the clock may have lost power;
   check the USB cable.

There is a faint Moon shape on a dark screen
   The main board has an AMOLED screen. A picture shown for a long time
   can leave a faint trace. This is expected for this clock and does
   not affect reading the time.
