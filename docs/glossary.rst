Glossary
========

The terms the running text resolves to: the lunar astronomy, the
rendering model, the device pipeline, and the project's own
jargon. The sources behind the numbers live in
:doc:`Sources and references <reference/sources>`.

Lunar astronomy
^^^^^^^^^^^^^^^

- **Body frame** — the Moon's rotating reference frame (pole = +z),
  the IAU_Moon rotation: the calibrated pole
  (RA 269.839977° / Dec 68.110423°) + the prime-meridian rate
  (13.17617251°/day). The frame a surface direction lives in before
  the camera sees it (:doc:`the geometry <science/geometry>`).
- **Subsolar point** — the surface point where the Sun is zenith
  (``subsolar_lon/lat``); its distance from the subearth point sets
  the terminator.
- **Subearth point** — the surface point under the observer
  (``subearth_lon/lat``); the visible pole of the disk.
- **Position angle (posangle)** — the angle of celestial north east
  of the observer's zenith, in degrees; the camera basis' in-plane
  rotation (:doc:`the geometry <science/geometry>` and :doc:`the dark side <science/darkside>`).
- **Phase angle** — the Sun–Moon–observer angle ``i``; the
  illumination is ``(1 + cos i) / 2`` (the ``illum``).
- **Libration** — the Moon's apparent wobble (geometric, not
  physical): the posangle sweep + the subearth point's excursion,
  which lets ~59 % of the surface be seen over a cycle. The cheap
  ephemeris carries it (the subsolar/subearth residual, :doc:`the geometry <science/geometry>`).
- **Synodic month** — 29.530589 days; the phase cycle (the
  clock's boot sweep played it until 2026-11-13, the knob companion
  at cold boot until 2026-11-14, :doc:`the animation pipelines <implementation/animations>`).
- **New-moon epoch** — the instant of zero phase; the retired boot
  sweep's start (the two-pass 6 h coarse + 20 min fine scan came down
  with it, :doc:`the animation pipelines <implementation/animations>`).

The rendering model
^^^^^^^^^^^^^^^^^^^

- **Base-sphere direction** — the per-pixel unit vector
  ``r = u·right + v·up + w·E`` (the bake's first step; the
  257-entry atan/row-frac table pass, :doc:`the C core <implementation/core-c>`).
- **Displaced-normal Lambert** — the per-pixel lighting model:
  ``L = clip(lns, 0, 1)`` where ``lns`` is the relief-normal sun dot
  — the mountain shadows at the terminator (:doc:`the geometry <science/geometry>` and :doc:`the lighting model <science/lighting>`).
- **Terminator** — the day/night boundary (``d = r·S = 0``); the
  displaced model's edge (the band-only A/B, :doc:`the lighting model <science/lighting>`).
- **Earthshine** — the phase-independent dark-side gradient
  (``R = A·x + B·x² + C·x³`` in the dark disk fraction ``x``); the
  ``dark_zero = 0`` dark side (:doc:`the dark side <science/darkside>`).
- **Backlit night side** — the *computed* dark side (``dark_zero = 1``):
  the albedo + relief lit from the anti-solar direction (``−S``) at a
  faint level (peak gray ~51) — the "the unilluminated side is fully
  calculated" decision (:doc:`the dark side <science/darkside>`).
- **dk_sup** — the dusk's dark-side bound (retired with the dusk
  2026-11-13; the bake parameter stays in the portable core — every
  device bake today passes 0): the dark side was written
  for ``d < dk_sup`` instead of ``d < 0``, killing the geometrically
  lit rim sliver (the "white ray") with the ``MOON_DK_MARGIN`` NN
  erosion (:doc:`the dark side <science/darkside>`).
- **Shade map** — the pre-shaded body-frame equirect map the dusk
  re-projected (the constant-projection pair, :doc:`the dark side <science/darkside>` and :doc:`the animation pipelines <implementation/animations>`; host-side callers only
  since the dusk's retirement).
- **Constant projection** — the property that made the dusk cheap:
  the bake's color depends only on the body direction + the epoch
  sun; the camera basis selects the view but never enters the color
  (:doc:`the dark side <science/darkside>`).
- **Tone LUT** — the 256-entry calibrated gray→gray curve (the fit
  to NASA's lit-region balance, :doc:`the lighting model <science/lighting>`); ``MOON_TONE_LUT``.
- **Tint (warm LUT)** — the display's gray→RGB565 warm cast
  (R ×1.038 / B ×0.933); the art pass's one display knob
  (``moon_gray565``, :doc:`the artistic pass <science/art-pass>` and :doc:`rotation and the LUT <implementation/rotate-lut>`).
- **Gate (image gate)** — the host validation: the C core's frames
  image-diffed against NASA SVS 5415 (≤ 12 MAE/frame) and the Python
  prototype (≤ 2 MAE/frame), the 32-frame set (:doc:`the image gate <verification/image-gate>`).

The device pipeline
^^^^^^^^^^^^^^^^^^^

- **Atomic frame** — the invariant: every moon-pipeline frame hits
  the panel as ONE whole-panel ``lv_refr_now`` under ``s_rgb_mtx``;
  the two sanctioned exceptions are the fade-to-black and the bake
  reveal's atomicity-per-state
  (:doc:`the architecture <implementation/architecture>` and :doc:`the
  bake reveal <implementation/bake-reveal>`).
- **s_rgb_mtx / s_panel_mtx** — the two locks: the *image* lock
  (bake/display of the moon buffer) and the *panel-io* lock
  (LVGL flushes, the 0x51 brightness)
  (:doc:`the architecture <implementation/architecture>`).
- **Spin** *(removed 2026-11-13)* — the former burn-in rotation: a
  once-baked frame re-projected by the Q15.15 gather at 8 fps and
  written *directly* to the panel (LVGL bypassed for that frame).
  Retired entirely — the home moon is plain LVGL, shown static
  (:doc:`the architecture <implementation/architecture>`).
- **Burn-in** *(accepted 2026-11-14)* — the AMOLED consequence of the
  static home: with the spin retired, a faint ghost of the moon is
  likely to settle into the S3 panel over weeks of always-on use — and
  the user deliberately counts it a feature, not a bug: it will likely
  not disturb reading the display
  (:doc:`the animation pipelines <implementation/animations>`).
- **Sweep** *(removed from the clocks 2026-11-13)* — the former boot
  animation: the last synodic month — card-less, 1344 tick-displayed
  frames at ``UI_MOON_ANIM_BAKE_PX``; with the TF sweep cache, the
  cycle's hour list replayed as the same tick-displayed frames —
  always ending on the live full-resolution final bake. Its COMPUTED
  animation stayed removed (every wake is static; with
  ``UI_MOON_BOOT_REPLAY=n`` so is boot); the knob sibling followed
  on 2026-11-14, retiring its own cold-boot sweep. What returned on
  2026-11-14 is the **boot replay** — pure playback of stored frames,
  no per-frame bake
  (:doc:`the animation pipelines <implementation/animations>` and :doc:`the knob display <implementation/knob-display>`).
- **Boot replay** *(``UI_MOON_BOOT_REPLAY``, added 2026-11-14)* — the
  boot presentation on the card-boards: a core-1 task plays ALL stored
  TF hour frames ascending on the boot black and stops on the newest,
  then the live-computed current phase lands as the final frame (one
  drain-witnessed atomic whole-panel flush per frame, no fade, the
  idle→sleep ladder held until the end leg; the final bake is
  **revealed** while it computes — see *Bake reveal*). With no usable
  cache the panel holds its boot black until the revealed final leg
  starts drawing; the knob (no SD socket) always takes that path
  (:doc:`the animation pipelines <implementation/animations>`).
- **Dusk** — the retired (2026-11-13) light-sleep entry: the
  constant-projection pole spin to the dark side (the shade map + the
  re-projects), the backlight dim to 25 %, the RTC countdown armed on
  the last frame. Light sleep is a **static dim frame** now (panel
  25 %, clock 20 % alpha, countdown armed at the entry — and the
  dim *is* the sleep indicator)
  (:doc:`the animation pipelines <implementation/animations>` and :doc:`the clock and sleep machine <implementation/clock-sleep>`).
- **phi\*** — the dusk's retired endpoint latitude (``atan2(−B, −A)``),
  the pole angle that put the visible disk fully in the dark side
  (:doc:`the animation pipelines <implementation/animations>`).
- **Hourly refresh** *(``UI_MOON_HOURLY_ENABLE``)* — the one live motion
  left on the home: once per hour the moon fades to black on the panel's
  own hardware brightness, a core-1 producer rebuilds the current-phase
  frame at the **hour centre** (a TF hour-cache fetch, else a full bake),
  that frame lands as ONE atomic whole-panel flush while the panel is still
  black, then the brightness ramps back — the clock, alarm row and seconds
  keep running through it. On a cache miss the rebuild is **revealed**
  instead (strip-by-strip atomic bands, fade-back at the first strip —
  see *Bake reveal*). Replaces the retired boot sweep; the TF hour
  cache is its card-side fetch
  (:doc:`the animation pipelines <implementation/animations>`).
- **Bake reveal** *(``UI_MOON_BAKE_REVEAL`` / ``KNOB_UI_BAKE_REVEAL``,
  added 2026-11-14)* — displaying the moon's computation while it runs:
  a computing frame goes to a black canvas and each completed strip
  lands as ONE atomic band flush (watermark from the producer, 50 ms
  tick consumer), so the moon draws itself north to south at the
  actual bake rate — the second sanctioned exception to the atomic-flush
  rule, as *atomicity-per-state*. Used by the hourly refresh's
  cache-miss bake and the boot replay's final leg (clock), and by the
  knob's boot bake (producer-interleaved, boot only); fetch hits never
  arm it
  (:doc:`the bake reveal <implementation/bake-reveal>`).
- **DPI FIFO underrun** — the P4 scanout failure: the DPI controller
  cannot fetch the strip from XIP PSRAM fast enough (the 103/65-row
  strip fix, :doc:`the P4 port <implementation/p4-port>`).
- **Strip** — the flush/write unit: the S3's 25-row (23,300 B)
  buffer, the P4's 103/65-row buffer (the DMA cap + the FIFO budget)
  (:doc:`the display pipeline <implementation/display>` and :doc:`the P4 port <implementation/p4-port>`).
- **Panel-io watchdog** — the ``panel_wdg`` task: a stuck panel-io
  (the lost-DMA-completion freeze, :doc:`the display pipeline <implementation/display>`) restarts the board
  after ``PANEL_IO_WATCHDOG_LIMIT_MS`` (3000 ms).
- **Fade-to-black** — one of the two sanctioned exceptions to the
  atomic-flush
  rule (the other is the *bake reveal*, above): the CO5300's *hardware*
  brightness ramp (0x51, 12 steps, 240 ms)
  writes no pixels, and the new frame's one atomic flush runs while the
  panel is still black. Two flows ride it — the screen switch
  (``UI_SCREEN_SWITCH_FADE``) and the hourly refresh's two legs
  (:doc:`the display pipeline <implementation/display>` and :doc:`the animation pipelines <implementation/animations>`).
- **Keep-mask** *(removed with the spin, 2026-11-13)* — the spin's
  former UI overlay: a full-screen ``lv_snapshot`` that kept the
  clock legible while the moon rotated beneath it under the direct
  writes.
- **90°-step orientation** — the tilt roll: whole-screen orientation
  in 90° steps (the int4-quantized g + the 1 s windowed mean + the
  integer dot/cross quadrant), the +90° mount base (:doc:`the clock and sleep machine <implementation/clock-sleep>`).
- **Departure wake** — the tilt-wake model: the wake fires on
  *departure* from a settled-pose EMA (two bars + a 2-sample
  streak), not raw per-sample deltas (:doc:`the clock and sleep machine <implementation/clock-sleep>`).
- **moon_assets** — the 2,560 K flash partition carrying the
  generated blob: the 864×432 equirect albedo + the 2048×1024 int8
  relief DEM, a 2,470,400 B combined image on the S3
  (:doc:`the binary-size chronology <verification/binary>` and :doc:`the build system <reference/build-system>`).
- **Relief gradient cache** — the one-time boot build of the DEM's
  quantized slopes into a packed uint16 array (the 2 PSRAM loads/px
  bake, :doc:`the C core <implementation/core-c>` and :doc:`performance <verification/performance>`).

Project jargon
^^^^^^^^^^^^^^^

- **The art pass** — the display-constant pass (the backlit dark
  side, the soft terminator, the warm tint) that is *deliberately*
  outside the gate's physics path (:doc:`the artistic pass <science/art-pass>`).
- **The standing rule** — CLAUDE.md's doc rule: after every change,
  fix the affected RST pages, regenerate the screenshots when screens
  changed, and commit the docs with the code (:doc:`the build system <reference/build-system>`).
- **The clean-tree gate** — the stop-hook that commits an in-flight
  working tree; the ``wip:`` commits in the project's history are
  its records (:doc:`the binary-size chronology <verification/binary>`).
- **Board identification** — the S3 and P4 claim ``/dev/ttyACM*``
  through their USB serial adapters (the knob's app MCU does the same
  via native USB; the knob's second-MCU plug orientation claims
  ``/dev/ttyUSB*`` instead); which port is which changes with
  USB topology, so identify before flashing (``lsusb`` / ``dmesg`` on
  plug-in, then ``esptool --port <port> chip_id`` — the procedure in
  :doc:`the boards <devices/index>`).
- **The wheel (v2)** — the shipped 12-sector hybrid password
  keyboard (:doc:`the round keyboard <keyboard>`).
- **The lab** — the interactive design lab at
  ``docs/_static/keyboard/round-keyboard-lab.html`` (the 13 operable
  input-method panels, each in its own widget,
  :doc:`the round keyboard lab <keyboard-lab>`).
- **uisim** — the host UI simulator: the real screens rendered to
  PNG with re-driven indev timestamps (``host_sim/uisim``).
- **moon_sim / moon_bench** — the host C-sim (the gate's C frames)
  and the host performance bench (the exact device core, :doc:`performance <verification/performance>`).
- **A/B** — the relief-on/off comparison (the PSRAM-load-latency
  floor, :doc:`performance <verification/performance>`).
