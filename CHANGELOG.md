## Unreleased

### Feat

- knob boot arm runs the family's transient WiFi screen during the sync window

### Fix

- the knob black flash was the retained panel frame overpainted moon-less
- knob wake no longer sits on black glass through the Wi-Fi + NTP window
- the knob "redraw loop" was a boot->crash->reboot cycle -- knob_boot's 4 KiB
  stack overflowed

## 0.2.0 (2026-11-24)

### Feat

- knob wake shows the persisted last frame before the fresh bake lands
- basic stand concept — minimal ring-on-a-post desk stand for the S3 466
- universal 3D-printed housing (A1) — parametric builder, dual audit gates,
  docs
- named-city time zones + observer-view moon tilt

### Fix

- gate the S3's AF fire path on the rule cache (P4 parity)
- clear the latched AF on alarm disarm; disarm a stale PCF arm on a
  missing-file boot

## 0.1.0 (2026-11-24)

### Feat

- acquire the docs glyph-census foundry faces with pins
- first-class moon asset acquisition for fresh clones
- **ui**: ZERO ANGLE wall-mount trim — arc-slider -> touch-usable 0..359 roller
- **ui**: the wall-mount base-angle trim joins the family (17th screen), 30-min
  alarm cap
- **ui**: the bake reveal stops hiding its work — 40 states on the S3, 50 on
  the P4
- **ui**: reveal the moon bake strip by strip while it computes
- **ui**: the home alarm row drops the leading zero from the hour
- **ui**: boot replay of the TF hour cache (new-moon cache policy)
- **knob**: retire the cold-boot sweep — every boot shows the static phase
- **ui**: the once-per-hour moon refresh with the hardware fade
- **ui**: light sleep becomes a static dim frame; drop the dusk
- **knob**: the iPhone-style battery icon at the home top (retire the NN%
  label)
- **ui**: the LCD panels drop the burn-in spin; digits hard-swap
- **knob**: sweep tap-abort + gated progress row on the shared home
- **ui**: one lv_roller for the family's Wi-Fi list — all three boards
- **knob**: home dial dims the panel; WiFi list becomes a 37 px roller
- **knob**: tap-commit fix + P3 weekday arc + P4 battery gauge
- **knob**: P2 — hub + settings via dial, store-driven ladder
- **knob**: P1 home parity — the family app on glass, dim parity, alarm TIMER
  wake
- **knob**: family seam (P0) — link the clock's shared clock/wifi/moon
- **knob**: caption is a local wall clock, wakes on tap, arms on NVS time
- **demo**: full on-device tour on all three boards + bench recording harness
- **ui**: TF-card bake cache for the synodic boot sweep
- **knob**: idle ladder — dim, deep sleep, EXT0 dial-A wake (K7e)
- **knob**: SNTP time truth with honest "no time" until a fix (K7d)
- **knob**: cold-boot synodic sweep, sky-event tint, day tick (K7c)
- **knob**: standalone moon display (K7b) + full companion removal
- **assets**: knob board target (K7a) — pipeline registry, gate plumbing,
  header
- **wheel**: knob dwell countdown ring + check-commit / X-discard keys
- **knob**: K4 — KEYBOARD/STEPPER mirror modes + UDP sender with mDNS discovery
- **companion**: K3 — clock-side UDP listener + injection API (P4
  device-verified)
- **knob**: K2 — STA join from baked spiffs credentials (device-verified)
- **knob**: K1 skeleton — sibling project, QSPI panel + CST816S + dial on
  screen
- **ui**: quadruple the boot sweep frame count (336 -> 1344)
- **moon**: per-board moon asset resolution -- Kconfig-driven albedo + native
  sweep bake
- **ui**: centre-anchor the next-alarm row (v2) + the wheel lit key is a ring
  segment again
- **ui**: re-add the light-sleep clock alpha (default 20%) + right-align the
  next-alarm row
- **moon**: the dusk lit fade -- the earth ray is completely removed
- **ui**: clock pair cells + pair roll -- the HH:MM digits fit their boxes
- **ui**: clock rollers -- the digit roll adopts the settings roller's motion
- **ui**: demo mode (MM:SS + screen rotation) + the settings hub ring radius
- **moon-event**: pre-calculated sky-event colors —
  harvest/blue/super-micro/blood
- **alarm-audio**: four selectable alarm sounds + settings screen, both boards
- the dusk kill margin (MOON_DUSK_KILL_MARGIN) -- kill the limb harder
- the per-digit odometer roll for the clock (replaces the dash WIP)
- one always-current shared clock widget + the UI_CLOCK_ALPHA_PCT option
- **ui**: complete the wheel v2 -- the 400 ms centre long press returns to the
  Wi-Fi list
- **ui**: complete the wheel v2 -- the 400 ms centre long press is the only way
  back to the Wi-Fi list
- **moon**: the artistic beat-NASA pass -- backlit dark side, soft terminator,
  warm LUT
- **p4**: add the P4 manual time entry (M5.6) -- the hub's clock icon + the SET
  TIME / DATE screen
- add the alarm ring -- the P4 ES8311 speaker via esp_codec_dev, the S3 no-op
  stub
- **p4**: add the software clock (M5.1/M5.2) -- esp_timer one-shots, NVS epoch
  backup
- **moon**: add the computed (backlit) night side
- **p4**: the P4 moon disk 618 -> 650 px + the spin seam-diagnostic hook
- **p4**: add the P4 UI upscale (M4) -- the UI_PX panel-scaled layout +
  fonts_p4
- **p4**: add the P4 moon scale-up (M3) -- MOON_PX 618 / MOON_R 309 / 103-row
  spin strips
- **p4**: add the M2 remote C6 Wi-Fi (esp_hosted 1.4.7 + esp_wifi_remote
  0.14.5)
- add the dual-board (S3/P4) scaffolding — board shim, per-target defaults, P4
  partition table
- **ui**: add the circular wheel keyboard for the Wi-Fi password entry (the
  13th screen)
- **ui**: add the screen-switch fade via the panel's hardware brightness (no
  pixels written)
- **ui**: add the direct-write spin, the fade-to-black switch, the alarm day
  prefix, the F057 exit
- **ui**: add the concentric (center-out) screen-switch reveal
- **ui**: add the two-digit alarm/pomodoro indicator and the alarm-save
  read-back log
- **settings**: timeouts in minutes (1..30), default 60 s
- **ui**: add the settings batch — range-arc day picker, exit restyle, WiFi
  rework
- **ui**: add the shared clock-style rollers + bottom-middle exit on
  sub-screens
- **ui**: add the pressable weekday pie slices + the pomodoro stopwatch at F2F2
- **ui**: add screen orientation in 90-degree steps (10 Hz gravity, 1 Hz mean)
- **ui**: add the Wi-Fi settings screen (AP scan list, selection; password
  entry deferred)
- **ui**: add the sleep-settings screens (runtime transition timers, animation
  toggles)
- **ui**: add the alarm settings, pomodoro interval, weekday-pie alarm time
  screens
- **ui**: add the settings hub (circular icon ring, concentric exit) and
  tap-to-open navigation
- **ui**: add the settings font subsets (FA +xmark/gear/stopwatch; weekday 40
  px; SSID 32 px)
- **clock**: add the alarm write/reload and the N>59 cron support
- **ui**: add airplane mode as the home screen and the transient Wi-Fi sync
  screen, retire MAIN
- **moon**: constant-projection dusk, tilt-driven clock roll, orbit off
- **moon**: boot-only sweep from black + the pole-axis sleep 'dusk' spin
- **moon**: anchor the wake sweep at the last new moon, with the
  240px/336-frame defaults
- **moon**: double the synodic wake sweep frame count (168 -> 336)
- **moon**: add the 168-frame wake sweep at a reduced-res bake and the
  alarm-dismiss sweep trigger
- **moon**: add the free phase-anchored spin, the wake sweep on every wake, the
  exact-pose park
- **ui**: replace the boot phase-sweep with a slow burn-in spin
- sphere model -- out-of-plane tilt orbits the moon (peek at the dark side)
- smooth row-sweep moon animation, working IMU, wake-replay test hook
- moon relief shadows, full-size boot animation, QMI8658 tilt spin
- moon phase display with boot animation
- simplified WiFi management with fast boot
- move ui tick timer to actions
- wire screens for init modes
- add more screens for init modes
- add minute and half minute interrupt timers
- implement screen switching logic between main-sleep-alarm screens
- add alarm functionality
- **clock**: add SNTP sync via Wi-Fi
- **ui**: update the colors and add the screens
- add minute tick for clock and sync to 0
- Add clock component

### Fix

- ride out storm-pressure no-mem strip bounces instead of restarting
- restore the truncated --utc tail of host_sim/main.c
- **knob**: route knob_moon stack to internal RAM (tick-ISR crash family)
- **ui**: settings display roller layout clears the exit; P4 hub ring R
  140->162
- **ui**: mount all three panels at 90 deg (UI_ORI_BASE_V10 900 family-wide)
- **ui**: pass the screen-switch frame through the sync-storm panel hold
- **knob**: register the alarm touch dismiss — the alarm screen was a one-way
  door
- **ui**: derive the clock-widget tick base from objects_t — kills the
  minute-tick LoadProhibited
- **knob**: the alarm screen's clock ticks — open the minute-tick gate the port
  never opened
- **knob**: bake the moon disk inside the seconds arc (clock containment)
- **knob**: the docked battery fill takes the theme's moon color
- **ui**: the sweep hands the codec scratch back at its last frame
- **knob**: the battery icon moves to the home bottom centre
- **ui**: the sweep cache runs thin instead of dropping the card path
- **knob**: the shared home bakes the disk centred and full-fill
- **ui**: rescue the starved screen-switch fade-ins
- **ui**: finalize the drain witness — serialize, heal in place, budget
  restarts
- **ui**: drain-verify every atomic whole-panel flush (S3)
- **ui**: return the TF codec scratch on card-less boots; don't clear the mask
  dirty flag
- **ui**: feed the idle task with real ticks in the bake loops; log the flush
  path
- **main**: run the hang watcher unpinned so a spinning core cannot starve it
- **ui**: bound LVGL's flush wait so a lost trans-done hand cannot pin a task
- **ui**: hold panel traffic across the WiFi scan session (task #30)
- **wifi**: drain DHCP before esp_wifi_stop (tcpip/softmac teardown race)
- **wifi**: survive the repeated init/deinit cycle (no abort, no handler leak)
- **ui**: hold panel io across the whole wake-sync radio session
- **ui**: drop panel writes during the wake-sync radio storm (S3 wake crash)
- **ui**: park the spin across wake/alarm 0x51 writes (lost-DMA stall)
- **clock**: kill the two stale-state bugs the user's bench pass caught
- **wifi**: re-acquire the radio when the manager tears it down mid-scan
- **clock**: fire the alarm-update callback on the 0-rule reload too
- **ui**: centre the Wi-Fi list family-wide — placeholder label + centred
  entries
- **knob**: 100 Hz systick like the S3 clock — the systimer snapshot crash
  family
- **knob**: yield the classify scan on a 20 ms wall-clock budget
- **knob**: yield the ERFA classify scan — boot-sweep WDT reset + ring fixes
- **knob**: bootstrap the home's UI + seconds lv_timers at boot
- **knob**: reentrant LVGL mutex wrapper — fix the tap-frozen device
- **knob**: the battery gauge font — digits + % need the SSID set
- **knob**: the main task needs 8192 B once the shared home builds on it
- **knob**: the TIMER-wake alarm arbitration's three bench-visible holes
- **moon**: never run the sky-events day scan on the LVGL task
- **tfcache**: make the fetch path allocation-free (S3 PSRAM head)
- **ui**: seed the UI keep-mask and front mirror as one atomic pair
- **ui**: the TF cache gray JPEG quality 32 -> 90
- **tfcache**: q32 flats + a direct-write 25 fps warm replay
- **ui**: widen the tour's tap settle past the fade-to-black switch
- **knob**: the boot-core LVGL race; bench probes for touch/dial/caption
- **ui**: the S3 TF card mounts on its own SPI host (GPSPI3)
- **ui**: the TF cache lives under the BSP mount point — /sdcard/moon_cache
- **clock**: idle timeout clamp 60..1800 s (align clock_idle with
  Kconfig/store/roller)
- **knob**: the dial is a UP/DOWN pulse pair — vendor bidi_switch_knob scheme
- **knob**: sign the detent from instantaneous B at the A rise
- **knob**: the settle filter could never accept an edge
- **ui**: per-board mount offset — the S3 AMOLED needs a further quarter turn
- **knob**: count the rising A edge only — one detent no longer cancels itself
- **knob**: the K4 bench bugs — encoder detent cancellation + RGB565 byte order
- **uisim**: the wheel hub-tap mirrors the current hub ring (R 140, n 6/7)
- **wifi**: retry the AP scan while it returns 0 APs
- **ui**: the clock pair cells -- zero the theme's 20 px pad, anchor the pairs
  colon-side
- **ui**: shared clock widget off-by-one after the alarm-sound screen root
- **sky-event**: sweep task stack-overflow on the P4 — classify scan outgrew
  the 8 KB stacks
- **design**: D_tip exponent typo — 1.383e6 -> 1.383e9 m
- the 2026-11-04 bugreport — orientation roots, unfilled-moon icon, wheel
  keyboard
- **ui**: the alarm ring task's stack 4096 -> 6144 B
- **ui**: the S3 spin seam fix -- one continuous 400x400 QSPI window write
  (max_transfer_sz 23,300 B)
- **p4**: one framebuffer and the NONE tear mode (the spin's CCW flicker, #13)
- **ui**: the fade-to-black now covers the settings sub-menu switches (every
  switch fades)
- **ui**: the sweep-aware idle re-arm + the AP scan's quiescent radio
- **ui**: snprintf the clock/alarm label setters (fix -Wstringop-truncation)
- **ui**: snprintf the clock/alarm label setters (the strncpy of the >32 B
  sources into the 32 B fields trips -Wstringop-truncation)
- **ui**: the settings-entry freeze — serialize 0x51 writes, hold the spin
  during the fade
- **ui**: fix the reveal's rotated ghost and gate the 2 s press orientation
  re-sync
- **ui**: fix the reveal's unrotated bloom and the 2 s press orientation
  re-sync
- **moon**: kill the dusk white ray (reproject the dark-side floor + map
  dk_sup)
- **moon**: kill the dusk rim sliver, cut the active-spin CPU, the WiFi screen
  only while syncing
- **bsp**: pace the LVGL task on the 10 ms UI tick (100 Hz tick rate)
- guard the CPU-stats hook on the run-time stats config
- fix the CPU-stats hook's window denominator
- **ui**: the boot hand-off only switches from the Wi-Fi screen
- **ui**: seed the orientation ref from the 1 s filtered mean
- **ui**: settings fixes — hub exit, wifi indicator, one-row sleep, centred
  HH:MM, day arc
- **moon**: the constant night floor replaces the flat-black far side
- **ui**: the settings demo writes Mon-Fri (0x3e), not Mon-Sat
- **ui**: fix the range-arc layout (letter clipping + roller collision)
- **moon**: use the flat-black night side on the device (dusk rim + dark phase)
- **ui**: +90 deg CW base orientation; keep the absolute pose across re-poses
- **ui**: fix the 180-degree quadrant decision and confirm the steps on a
  sustained second
- **ui**: fix the clock roll dt (us vs ms) -- the ramp actually runs at 5 deg/s
  now
- **ui**: fix the clock roll (centered pivot, calm 5 deg/s ramp, 0.25 Hz LPF,
  LVGL clib allocator)
- **moon**: raise the tilt settled-reference EMA (0.1 -> 0.2) for the drift
  ceiling
- **moon**: zero the whole geometric night side in the sweep montage
- **clock**: disarm the stale RTC alarm when no rules are loaded
- **moon**: use the flat-black dark side for the wake-sweep montage frames
- don't block the LVGL task on the wake WiFi reconnect
- **ui**: make the frame display atomic and the spin baseline steady across
  transitions
- reset the spin strip counter at frame end (display was stuck after frame 0)
- pin the LVGL task to core 0 (producer starved it on core 1)
- alarm exit, fixed moon orientation, denser boot animation
- alarm screen could not be exited while awake (one-way door)
- tilt is the moon's input and wake path; countdown expiry by time
- terminator shadows match NASA via full-disk unclipped relief + 2048 DEM
- return from alarm if wifi is not available
- **clock**: Read alarm time directly from RTC hardware registers

### Refactor

- **ui**: the home boots straight to the static phase; drop the boot sweep
- **ui**: remove the burn-in spin subsystem
- **companion**: single-source the wheel registers so the knob can type digits
- Decouple clock and UI components using callback pattern
- clock separate into files

### Perf

- **ui**: defer the wake re-bake to a core-1 task — wake answers instantly
- **ui**: pipeline the TF replay — prefetch on core 0, run-list keep-mask
- **ui**: add the spin coast-skip for the sub-pixel settled frames
- **ui**: stop the moon spin render on the moon-less screens
- **moon**: add the packed 16-bit relief cache and the 200px sweep default
- **moon**: add the two-core wake sweep and the ping-pong display buffers
- **moon**: skip the albedo load for the dark-side pixels in the bake
- **moon**: add the precomputed relief-gradient cache and the 28-frame wake
  sweep
- land the tilt orbit as one frame, like the spin (not a 2.4s wipe)
- split the boot-anim bake across both cores (task #22)
- inline + 4-way unroll the bake pixel loop (task #22)
