Glossary
========

.. brief:
   Reader: anyone who meets an unfamiliar word on this site, from a school student to a firmware developer.
   They already know: nothing specific; each entry starts in plain language.
   After this page they can: understand a term well enough to keep reading, and follow the link to the page that explains it fully.
   Page type: reference.

This page defines the words the rest of the site uses.
Each entry starts with a plain sentence; the technical detail follows.
The groups are the Moon itself, the hardware, how the picture is drawn, how the firmware puts it on the screen, and the project's own words.
The sources behind the numbers are in :doc:`Sources and references <reference/sources>`.

The Moon and its motion
^^^^^^^^^^^^^^^^^^^^^^^

- **Phase**: the shape of the lit part of the Moon that we see, from new moon (nothing lit) through first quarter and full moon back to new moon.
  It depends on the angle between the Sun and the Moon as seen from Earth (see *Phase angle*).
- **Synodic month**: the time from one new moon to the next, 29.530589 days.
  It is the length of one full cycle of phases.
- **New-moon epoch**: the moment of a new moon, when the phase is zero.
  The firmware finds the last one before the current time; the TF hour cache clears every frame older than it (:doc:`the animation pipelines <implementation/animations>`).
- **Terminator**: the line on the Moon between day and night.
  Points on it see the Sun exactly on their horizon.
  In the model it is where ``d = r·S = 0``, the edge of the displaced lighting model (the band-only A/B in :doc:`the lighting model <science/lighting>`).
- **Earthshine**: sunlight that bounces off the Earth and lights the Moon's night side very faintly.
  In the code, the earthshine model is a phase-independent dark-side gradient ``R = A·x + B·x² + C·x³`` in the dark disk fraction ``x``, used when ``dark_zero = 0``.
  The device bakes use the *backlit night side* instead (:doc:`the dark side <science/darkside>`).
- **Libration**: a slow apparent wobble of the Moon that lets us see a little way round its edges.
  It comes from the shape and tilt of the orbit, not from the Moon itself rocking, and it shows about 59 % of the surface over a cycle.
  In the model it is the posangle sweep plus the excursion of the subearth point; the cheap ephemeris carries it (the subsolar/subearth residual, :doc:`the geometry <science/geometry>`).
- **Ephemeris**: a table or formula that gives where a body in the sky is at a given time.
  The clock evaluates published formulas for the Sun, Earth and Moon on the chip, with no internet connection (:doc:`the geometry <science/geometry>`).
- **Phase angle**: the angle Sun–Moon–observer, ``i``.
  The lit fraction of the disk is ``(1 + cos i) / 2`` (the ``illum``).
- **Subsolar point**: the place on the Moon where the Sun stands straight overhead (``subsolar_lon/lat``).
  Its distance from the subearth point sets where the terminator falls.
- **Subearth point**: the place on the Moon where the Earth stands straight overhead (``subearth_lon/lat``).
  It is the centre of the disk we see.
- **Body frame**: a set of axes fixed to the Moon and turning with it, with +z along the Moon's pole.
  It is the IAU_Moon rotation: the calibrated pole (RA 269.839977° / Dec 68.110423°) plus the prime-meridian rate (13.17617251°/day).
  A surface direction lives in this frame before the camera sees it (:doc:`the geometry <science/geometry>`).
- **Position angle (posangle)**: how far the Moon's north is turned, measured east of the observer's zenith, in degrees.
  It is the in-plane rotation of the camera basis (:doc:`the geometry <science/geometry>` and :doc:`the dark side <science/darkside>`).
- **Parallactic angle**: how far celestial north is turned from straight up at the observer's horizon.
  It is ``q = atan2(sin H, tan φ cos δ − sin δ cos H)`` from the hour angle ``H``, the site latitude ``φ`` and the Moon's declination ``δ``.
  It turns a north-up disk into the view an observer in that city has; it is the in-plane rotation of the *observer view* (:doc:`the observer view <implementation/observer-view>`).

The hardware
^^^^^^^^^^^^

- **Microcontroller**: a small, cheap computer on a single chip, with its processor, memory and radio together.
  The clocks run on Espressif ESP32-S3 and ESP32-P4 chips (:doc:`the boards <devices/index>`).
- **Firmware**: the program stored on the microcontroller that runs when it powers up.
  For this project it is built with ESP-IDF and LVGL from the source in ``components/`` and ``main/`` (:doc:`the build system <reference/build-system>`).
- **Flash**: memory on the board that keeps its contents without power.
  It holds the firmware, the Moon maps (the ``moon_assets`` partition) and the saved settings (:doc:`the build system <reference/build-system>`).
- **PSRAM**: extra working memory (pseudo-static RAM) next to the chip.
  It is larger and slower than the chip's internal RAM; the Moon maps and frames live there.
- **AMOLED**: a screen type in which every pixel makes its own light, so black pixels are fully off.
  The S3 board has one; a picture shown for a long time can leave a faint trace (see *Burn-in*).
- **NTP**: the Network Time Protocol, the standard way to get the exact time from a server on the internet.
  The clock uses it over Wi-Fi and then switches the radio off (:doc:`the clock and sleep machine <implementation/clock-sleep>`).
- **RTC**: real-time clock, a small chip or circuit that keeps counting time between NTP updates.
  The S3 board has a separate PCF85063A RTC chip; the other boards use the main chip's low-power timer.

How the picture is computed
^^^^^^^^^^^^^^^^^^^^^^^^^^^

- **Albedo**: how light or dark the surface is at a place, from 0 (black) to 1 (white).
  The clock stores it as a map of the whole Moon, made from NASA Lunar Reconnaissance Orbiter data (:doc:`the lighting model <science/lighting>`).
- **Lambert's cosine law**: the rule that a surface looks brightest when light hits it head-on and dimmer as the light comes in at a slant.
  The brightness is the albedo times the cosine of the angle between the Sun and the surface's vertical (:doc:`the lighting model <science/lighting>`).
- **Displaced-normal Lambert**: the clock's lighting model, Lambert's law applied with the surface tilted by the height map.
  Per pixel, ``L = clip(lns, 0, 1)``, where ``lns`` is the dot product of the relief normal and the Sun direction.
  It draws the mountain shadows at the terminator (:doc:`the geometry <science/geometry>` and :doc:`the lighting model <science/lighting>`).
- **Bake**: the step that computes one complete picture of the Moon from the maps and the Sun and Earth directions.
  It runs pixel by pixel and takes about 1.3 s for a full frame on the S3 (:doc:`the C core <implementation/core-c>`).
- **Base-sphere direction**: the unit vector from the Moon's centre to the point a pixel shows, ``r = u·right + v·up + w·E``.
  It is the bake's first step (the 257-entry atan/row-frac table pass, :doc:`the C core <implementation/core-c>`).
- **Backlit night side**: the clock's computed dark side (``dark_zero = 1``).
  The albedo and relief are lit from the direction opposite the Sun (``−S``) at a faint level (peak gray about 51), so craters stay visible on the night side.
  This follows the decision that the unlit side is fully calculated (:doc:`the dark side <science/darkside>`).
- **dk_sup**: a bake parameter of the portable core that moves the dark-side bound from ``d < 0`` to ``d < dk_sup``.
  Every device bake passes 0, the plain ``d < 0`` terminator; the parameter's origin is in :doc:`the design history </project/history>` (:doc:`the dark side <science/darkside>`).
- **Shade map**: a pre-shaded map of the Moon in the body frame (equirectangular), which can be re-projected to any view.
  Only host-side callers use it (:doc:`the dark side <science/darkside>` and :doc:`the animation pipelines <implementation/animations>`).
- **Constant projection**: the property that a baked pixel's colour depends only on its body direction and the Sun at the epoch.
  The camera basis chooses the view but never enters the colour, which is what makes a shade map re-projectable (:doc:`the dark side <science/darkside>`).
- **Tone LUT**: a look-up table (LUT) of 256 entries that maps each computed gray to a displayed gray.
  It is fitted to NASA's balance in the lit region (:doc:`the lighting model <science/lighting>`); ``MOON_TONE_LUT``.
- **Tint (warm LUT)**: the display's gray→RGB565 warm cast (R ×1.038 / B ×0.933).
  It is the art pass's one display setting (``moon_gray565``, :doc:`the artistic pass <science/art-pass>` and :doc:`rotation and the LUT <implementation/rotate-lut>`).
- **Image gate (gate)**: the automated test that checks the drawing against NASA's pictures.
  It runs the C core on a PC for the 32-frame set and image-diffs each frame against NASA SVS 5415 (at most 12 mean absolute error per frame) and against the Python prototype (at most 2 per frame) (:doc:`the image gate <verification/image-gate>`).

Putting frames on the screen
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

- **Atomic frame**: the rule that a new Moon picture always reaches the screen in one piece, never half old and half new.
  Every moon-pipeline frame goes to the panel as one whole-panel ``lv_refr_now`` under ``s_rgb_mtx``.
  The one sanctioned exception is the fade-to-black, a hardware brightness ramp that writes no pixels (:doc:`the architecture <implementation/architecture>` and :doc:`the compute progress <implementation/compute-progress>`).
- **s_rgb_mtx / s_panel_mtx**: the two locks.
  ``s_rgb_mtx`` is the image lock (bake and display of the Moon buffer); ``s_panel_mtx`` is the panel-io lock (LVGL flushes, the 0x51 brightness) (:doc:`the architecture <implementation/architecture>`).
- **Fade-to-black**: the screen dims to black, changes, and brightens again.
  It uses the CO5300's hardware brightness ramp (0x51, 12 steps, 240 ms), which writes no pixels; the new frame's one atomic flush runs while the panel is still black.
  Two flows use it: the screen switch (``UI_SCREEN_SWITCH_FADE``) and the light-sleep dim/wake ramp.
  The hourly refresh does not; the old frame stays up the whole cycle (:doc:`the display pipeline <implementation/display>` and :doc:`the animation pipelines <implementation/animations>`).
- **Hourly refresh** (``UI_MOON_HOURLY_ENABLE``): once an hour the clock draws a fresh Moon; it is the only live motion on the home screen.
  A core-1 producer rebuilds the current-phase frame at the hour centre, from a TF hour-cache fetch or else a full bake.
  The old frame stays up the whole cycle, with no fade and no 0x51 traffic, and the finished frame lands as one atomic whole-panel flush in the bright panel.
  On a cache miss the seconds arc shows the bake's rows watermark as a progress bar; a hit runs no strip loop, so the arc shows real seconds (:doc:`the animation pipelines <implementation/animations>`).
- **Compute progress**: while a new Moon is being computed, the seconds ring shows how far the work has got, and the old picture stays on screen.
  The producer bakes in strip chunks and publishes a rows-completed watermark.
  The home and Wi-Fi **seconds arc** reads it as a progress bar (``progress * 60 / 100``; a plain LVGL invalidate that snaps back to real seconds on the next tick).
  Where the panel would otherwise be dark, the phase disc stands in.
  Every image that lands is one atomic whole-panel flush, and the partial bounce buffer is never displayed, so there is no heal path (:doc:`the compute progress <implementation/compute-progress>`).
- **Phase disc**: a quick, plain stand-in picture of the current phase, shown while the first real Moon is still being computed.
  ``moon_bake_phase_dummy`` (core, shared verbatim with host and knob) projects the phase as a flat sphere: the real bake's geometry with no albedo, no relief and no events.
  The lit side goes through the tone LUT (a full moon is a uniform bright disc with a Lambert limb falloff); the dark side is plain black.
  It takes tens of ms against about 1.3 s for the full bake.
  It shows at boot (before the current frame) and at the knob's first boot with no saved frame, and never when a computed frame is present (:doc:`the compute progress <implementation/compute-progress>`).
- **Boot replay** (``UI_MOON_BOOT_REPLAY``): at start-up the clock plays back the Moon pictures it saved over the past hours, then shows the current one.
  On the boards with a card, a core-1 task plays all stored TF hour frames in ascending order and stops on the newest; then the live-computed current phase lands as the final frame.
  Each frame is one drain-witnessed atomic whole-panel flush with no fade, and the idle→sleep ladder is held until the end leg.
  While the final bake computes, the seconds arc shows its rows watermark (see *Compute progress*).
  While the first frames prepare, the panel shows the phase disc instead of black; with no usable cache there is no replay and the disc holds until the final bake lands.
  The knob (no SD socket) computes at every boot. With no saved last frame on glass it publishes the disc first; with ``KNOB_UI_LAST_FRAME`` it shows the persisted ``/spiffs/moon_frame.bin`` first. In both cases the fresh bake swaps over it (:doc:`the animation pipelines <implementation/animations>` and :doc:`the knob display <implementation/knob-display>`).
- **Observer view** (``UI_MOON_OBSERVER_ENABLE``): the Moon on screen is turned to match how it looks from the city of your time zone.
  Every gray→RGB565 build leg gathers the north-up baked frame through the Q15.15 in-plane rotation at the Moon's parallactic angle for the active zone's reference city.
  So the crescent lies down at dusk and stands up at midnight, and the tilt follows the zone's local time.
  The bake stays north-up (the gate's frames and the TF hour cache are zone-agnostic).
  The gather runs at publish, so the seconds-arc progress (the north-up strip loop's rows watermark) is honest with or without the tilt.
  A changed zone re-gathers the resting frame in place (``moon_ui_tz_changed``) (:doc:`the observer view <implementation/observer-view>`).
- **Strip**: the block of rows the firmware writes to the panel at a time.
  The S3 uses a 25-row (23,300 B) buffer and the P4 a 103/65-row buffer, set by the DMA (direct memory access) cap and the FIFO budget (:doc:`the display pipeline <implementation/display>` and :doc:`the P4 port <implementation/p4-port>`).
- **DPI FIFO underrun**: the P4's picture breaks up because the display controller cannot read the strip from XIP PSRAM fast enough.
  The fix is the 103/65-row strip (:doc:`the P4 port <implementation/p4-port>`).
- **Panel-io watchdog**: the ``panel_wdg`` task, which restarts the board when the panel stops responding.
  A stuck panel-io (the lost-DMA-completion freeze, :doc:`the display pipeline <implementation/display>`) triggers it after ``PANEL_IO_WATCHDOG_LIMIT_MS`` (3000 ms).
- **Burn-in**: a faint ghost of a picture that stays on an AMOLED screen after it has been shown for a long time.
  The S3 shows the Moon statically, so a faint Moon is likely to settle into its panel over weeks of always-on use.
  The project counts this as a feature, not a bug, because it will likely not disturb reading the display (:doc:`the animation pipelines <implementation/animations>`).
- **90°-step orientation**: the whole screen turns in 90° steps to stay upright when the clock is turned.
  It uses the int4-quantized gravity vector, a 1 s windowed mean and an integer dot/cross quadrant test, with a +90° mount base (:doc:`the clock and sleep machine <implementation/clock-sleep>`).
- **Departure wake**: the rule for waking the clock by movement.
  The wake fires when the pose departs from a settled-pose average (an EMA with two bars and a 2-sample streak), not on raw per-sample changes (:doc:`the clock and sleep machine <implementation/clock-sleep>`).
- **moon_assets**: the flash partition, 2,560 K, that holds the Moon maps.
  It carries the generated blob: the 864×432 equirectangular albedo and the 2048×1024 int8 relief DEM (digital elevation model), a 2,470,400 B combined image on the S3 (:doc:`the binary-size chronology <verification/binary>` and :doc:`the build system <reference/build-system>`).
- **Relief gradient cache**: a table of the height map's slopes, built once at boot so each bake pixel needs fewer memory reads.
  It packs the DEM's quantized slopes into a uint16 array, which brings the bake to 2 PSRAM loads per pixel (:doc:`the C core <implementation/core-c>` and :doc:`performance <verification/performance>`).

Project words
^^^^^^^^^^^^^

- **The art pass**: the settings that make the Moon look good on the display but are not physics.
  It covers the backlit dark side, the soft terminator and the warm tint, and it sits deliberately outside the gate's physics path (:doc:`the artistic pass <science/art-pass>`).
- **The standing rule**: the project rule in CLAUDE.md that every change updates the docs.
  After every change, fix the affected RST pages, regenerate the screenshots when screens changed, and commit the docs with the code (:doc:`the build system <reference/build-system>`).
- **The clean-tree gate**: the stop-hook that commits an in-flight working tree.
  The ``wip:`` commits in the project's history are its records (:doc:`the binary-size chronology <verification/binary>`).
- **Board identification**: finding out which USB port belongs to which board before you flash it.
  The S3 and P4 claim ``/dev/ttyACM*`` through their USB serial adapters; the knob's app MCU does the same via native USB, and the knob's second-MCU plug orientation claims ``/dev/ttyUSB*`` instead.
  Which port is which changes with USB topology, so identify before flashing: ``lsusb`` / ``dmesg`` on plug-in, then ``esptool --port <port> chip_id`` (the procedure is in :doc:`the boards <devices/index>`).
- **The wheel (v2)**: the round on-screen keyboard used to type a Wi-Fi password, with 12 letter sectors around the edge (:doc:`the round keyboard <keyboard>`).
- **The lab**: the interactive design study of 13 ways to type on the round screen, at ``docs/_static/keyboard/round-keyboard-lab.html``.
  Each input method is an operable panel in its own widget (:doc:`the round keyboard lab <keyboard-lab>`).
- **uisim**: the UI simulator that runs the real screens on a PC and saves them as PNG pictures.
  It re-drives the input device with simulated timestamps (``host_sim/uisim``).
- **moon_sim / moon_bench**: two PC programs built from the device's Moon code.
  ``moon_sim`` is the host C simulator that produces the gate's C frames; ``moon_bench`` is the host performance bench running the exact device core (:doc:`performance <verification/performance>`).
- **A/B**: a comparison of the bake with relief on and off, which measures the floor set by PSRAM load latency (:doc:`performance <verification/performance>`).

Retired features (the spin, the dusk, the boot sweep, the bake reveal) are described in :doc:`the design history </project/history>`.
