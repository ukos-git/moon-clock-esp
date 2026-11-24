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
- **Parallactic angle** — the angle of celestial north east of the
  vertical at the *observer's* horizon, ``q = atan2(sin H, tan φ cos δ
  − sin δ cos H)`` from the hour angle ``H``, the site latitude ``φ``
  and the Moon's declination ``δ``; what turns a north-up disk into
  the view an observer at that city has (the in-plane rotation of the
  *observer view*, :doc:`the observer view <implementation/observer-view>`).
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
  the ONE sanctioned exception is the fade-to-black (the hardware
  brightness ramp, which writes no pixels — the bake reveal's second
  exception, its atomicity-per-state band states, retired 2026-11-24
  with the compute-progress change)
  (:doc:`the architecture <implementation/architecture>` and :doc:`the
  compute progress <implementation/compute-progress>`).
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
  TF hour frames ascending and stops on the newest, then the
  live-computed current phase lands as the final frame (one
  drain-witnessed atomic whole-panel flush per frame, no fade, the
  idle→sleep ladder held until the end leg; while the final bake
  computes the seconds arc sweeps its rows watermark as a progress
  bar — see *Compute progress*). While the first frames prepare the
  panel shows the quick **phase disc** (*Phase disc*) instead of
  black; with no usable cache the replay is simply absent and the disc
  holds until the final bake lands. The knob (no SD socket) computes at
  every boot — with no saved last frame on glass it publishes the disc
  first and the fresh bake swaps over it; ``KNOB_UI_LAST_FRAME`` shows
  the persisted ``/spiffs/moon_frame.bin`` first and the fresh bake
  swaps over it
  (:doc:`the animation pipelines <implementation/animations>` and
  :doc:`the knob display <implementation/knob-display>`).
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
  left on the home: once per hour a core-1 producer rebuilds the
  current-phase frame at the **hour centre** (a TF hour-cache fetch,
  else a full bake) with the **old frame up the whole cycle** (no fade,
  no 0x51 traffic — the fade retired 2026-11-24, the compute-progress
  change), the finished frame landing as ONE atomic whole-panel flush in
  the bright panel; the seconds arc sweeps the bake's rows watermark as
  a progress bar on a cache miss (a hit runs no strip loop, so the arc
  shows real seconds). Replaces the retired boot sweep; the TF hour
  cache is its card-side fetch
  (:doc:`the animation pipelines <implementation/animations>`).
- **Compute progress** *(replaced the bake reveal, 2026-11-24)* —
  showing the moon's computation while it runs without displaying it
  strip-wise: the producer bakes strip-chunked and publishes a
  rows-completed watermark (the reveal's repurposed producer), the
  home/wifi **seconds arc** reads it as a progress bar (``progress * 60
  / 100``; plain LVGL invalidate, snaps back to real seconds the next
  tick), and where the panel would otherwise be dark the **phase disc**
  stands in. Every image that lands stays ONE atomic whole-panel flush —
  the atomic-flush rule's second exception is gone, and with it the
  heal path (the partial bounce is never displayed)
  (:doc:`the compute progress <implementation/compute-progress>`).
- **Bake reveal** *(``UI_MOON_BAKE_REVEAL`` / ``KNOB_UI_BAKE_REVEAL``,
  added 2026-11-14, retired 2026-11-24)* — the predecessor of
  *Compute progress*: a computing frame displayed as a black canvas +
  one atomic strip band per 50 ms tick (the *atomicity-per-state*
  exception to the atomic-flush rule). Its display side (band state
  machine, pacing, heal, the hourly fade choreography) is deleted —
  the strip loop with the rows watermark survived as the progress
  source, and the 2026-11-14/15 bench numbers remain the accepted
  bake-cost measurements
  (:doc:`the compute progress <implementation/compute-progress>`).
- **Phase disc** *(added 2026-11-24)* — the quick dummy image for the
  dark computation windows: ``moon_bake_phase_dummy`` (core, shared
  verbatim with host + knob) projects the current phase as a flat
  sphere — the real bake's geometry with no albedo, no relief, no
  events; lit side through the tone LUT (full moon = a uniform bright
  disc with a Lambert limb falloff), dark side plain black. Tens of ms
  vs the ~1.3 s full bake; shown at boot (before the now-frame) and at
  the knob's first boot with no saved frame; NEVER shown when a
  computed frame is present
  (:doc:`the compute progress <implementation/compute-progress>`).
- **Observer view** *(``UI_MOON_OBSERVER_ENABLE``, added 2026-11-17)*
  — the timezone's payoff: every gray→RGB565 build leg gathers the
  north-up baked frame through the Q15.15 in-plane rotation at the
  Moon's parallactic angle for the active timezone's reference city,
  so the crescent lies down at dusk and stands up at midnight and the
  tilt follows the zone's local time. The bake stays north-up (the
  gate's frames, the TF hour cache zone-agnostic); the compute bake's
  rows watermark is the north-up strip loop's, so the seconds-arc
  progress is honest with or without the tilt (the gather runs at
  publish); a changed zone re-gathers the resting frame in place
  (``moon_ui_tz_changed``)
  (:doc:`the observer view <implementation/observer-view>`).
- **DPI FIFO underrun** — the P4 scanout failure: the DPI controller
  cannot fetch the strip from XIP PSRAM fast enough (the 103/65-row
  strip fix, :doc:`the P4 port <implementation/p4-port>`).
- **Strip** — the flush/write unit: the S3's 25-row (23,300 B)
  buffer, the P4's 103/65-row buffer (the DMA cap + the FIFO budget)
  (:doc:`the display pipeline <implementation/display>` and :doc:`the P4 port <implementation/p4-port>`).
- **Panel-io watchdog** — the ``panel_wdg`` task: a stuck panel-io
  (the lost-DMA-completion freeze, :doc:`the display pipeline <implementation/display>`) restarts the board
  after ``PANEL_IO_WATCHDOG_LIMIT_MS`` (3000 ms).
- **Fade-to-black** — the ONE sanctioned exception to the
  atomic-flush rule (the bake reveal's second exception retired
  2026-11-24): the CO5300's *hardware*
  brightness ramp (0x51, 12 steps, 240 ms)
  writes no pixels, and the new frame's one atomic flush runs while the
  panel is still black. Flows ride it — the screen switch
  (``UI_SCREEN_SWITCH_FADE``) and the light-sleep dim/wake ramp (the
  hourly refresh stopped riding it on 2026-11-24: the old frame stays
  up the whole cycle)
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
