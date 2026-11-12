The display pipeline
====================

This page is the one a moon clock's design is actually made of:
a 466×466 RGB565 QSPI panel driven by a dual-core CPU whose display
DMA can move **32 KiB at a time**, whose PSRAM is **not DMA-capable**,
and whose panel io can, on rare occasions, **lose a DMA completion and
hang the bus forever**. Every mechanism on this page — the 25-row
strips, the 23,300-byte transfer cap, the panel mutex, the direct
spin write, the watchdog — is a consequence of those three facts.

The panel and the numbers that constrain it
-------------------------------------------

The S3 board's panel (the Waveshare ESP32-S3-Touch-AMOLED-1.75,
:doc:`the S3 board <../devices/s3>`) is the CO5300: 466×466, RGB565, driven over
QSPI by the ESP32-S3's **SPI2_HOST** peripheral (the "GPSPI" bus in
the IDF's naming). The numbers that matter:

.. list-table::
   :header-rows: 1
   :widths: 22 44 34

   * - Quantity
     - Value
     - Consequence
   * - Pixel format
     - RGB565, 2 B/px
     - one 466-px row = 932 B
   * - One strip (BSP ``buffer_height 25``)
     - 466×25×2 = **23,300 B**
     - the LVGL flush strip size, *and* the io driver's
       ``max_transfer_sz``
   * - S3 SPI DMA cap
     - **32 KiB per descriptor**
     - a single transfer may not exceed 32,768 B
   * - Largest DMA-capable internal-RAM block
     - ~31 KiB
     - the DMA staging copy must fit in *internal* RAM
   * - PSRAM
     - 8 MB octal, **not SPI-DMA-capable**
     - every PSRAM strip is **staged through
       internal RAM** before the DMA runs

The last three rows are the whole story. ``esp_lcd_panel_io_spi``
cannot DMA from PSRAM on this silicon: for every strip it copies the
strip into an internal-RAM descriptor buffer and *then* DMAs. Two
limits follow.

1. **The strip must be ≤ 32 KiB** — the DMA descriptor's transfer
   size. 466×25×2 = 23,300 B fits; the BSP's *original*
   ``buffer_height 50`` (46,600 B) does not.
2. **The staging copy needs a ~23.3 KB *contiguous internal* block,
   per in-flight transfer** — and the internal heap of an S3 with
   PSRAM enabled is a few hundred KB of a 512 KB SRAM, shared with
   stacks, the heap allocator's own metadata, and the LVGL task.

The 50-row failure (and the fix)
--------------------------------

The vendored BSP (``components/esp32_s3_touch_amoled_1_75``)
shipped with ``buffer_height = 50``. With it, a whole-panel flush —
19 strips of 46,600 B — returned ``ESP_ERR_NO_MEM`` partway through:
the io driver could not find a 46.6 KB contiguous internal block for
the staging copy, and with one strip in flight and the queue full,
the next strip's allocation failed. The fix was two lines in the
vendored BSP:

.. code-block:: c
   :caption: components/esp32_s3_touch_amoled_1_75 — display init (the two constraints)

   /* PSRAM is NOT SPI-DMA-capable on the S3: the panel io stages every
    * strip into an internal-RAM copy before the DMA runs. The S3 SPI
    * DMA descriptor caps one transfer at 32 KiB, so a strip must be
    * <= 16384 px (466x25x2 = 23,300 B fits; 466x50x2 = 46,600 B does
    * not -- the original buffer_height 50 NO_MEM'd mid-flush). The
    * staging copy needs a ~23.3 KB contiguous INTERNAL block per
    * in-flight strip, so the queue depth stays 1 (CONFIG_BSP_LCD_TRANS_QUEUE_DEPTH):
    * a 2-deep queue needs two ~23.3 KB blocks at once. */
   cfg.buffer_height = 25;
   io_cfg.max_transfer_sz = 466 * 25 * 2;  /* 23,300 B */

and ``CONFIG_BSP_LCD_TRANS_QUEUE_DEPTH = 1`` in the defaults. With 25
rows and queue depth 1, a whole-panel flush is 19 sequential
23,300-B transfers, each with one internal-RAM staging copy, and the
flush completes in ~175 ms.

The invariant this enforces: **keep ``buffer_height`` at 25 and the
flush queue depth at 1.** Raising either "to go faster" re-creates the
NO_MEM, and it fails in the ugliest possible way — partway through a
screen switch, so the screen shows a half-updated image *and* LVGL is
left with a failed refresh (the classic "display froze after a
settings screen" symptom that cost this project a full debug day).

The LVGL path: strips under ``s_rgb_mtx``
------------------------------------------

LVGL renders the invalidated area into draw buffers, then the adapter
flushes them to the panel strip by strip. Every flush strip goes
through ``esp_lcd_panel_draw_bitmap`` — which is exactly what the
pipeline exploits. At ``moon_ui_init`` the project *wraps* the panel
vtable's ``draw_bitmap`` member:

.. code-block:: c
   :caption: components/ui/moon_ui.c (the draw_bitmap wrapper)

   static esp_err_t moon_panel_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start,
                                          int x_end, int y_end, const void *color_data) {
       (void)panel;
       xSemaphoreTake(s_panel_mtx, portMAX_DELAY);
       if (s_front && /* strip overlaps the moon area */) {
           /* refresh the front mirror for the strip about to be
            * overwritten (the front content AFTER this write) */
           ...
       }
       r = moon_panel_draw_wait(panel, x_start, y_start, x_end, y_end, color_data);
       xSemaphoreGive(s_panel_mtx);
       return r;
   }

The wrapper does two jobs at once:

1. **Serialization** — every LVGL flush strip takes ``s_panel_mtx``,
   the same mutex the spin's direct write and the 0x51 brightness
   writes take. The panel io's own bus lock serializes the actual
   transfers; the mutex is what keeps *this* project's code from
   queueing a 0x51 behind a strip that might never complete (the
   freeze, below).
2. **The front mirror** — for every strip that overlaps the moon
   area, the wrapper copies the strip into ``s_front`` *after* the
   write (the panel content the strip now holds). The spin's
   composite reads ``s_front`` as "what LVGL is currently showing in
   the moon area" — the clock label, the seconds arc — so the spin's
   direct write can keep them in place (the UI keep-mask, below).

The atomic whole-panel flush (``moon_ui_screen_switched``) is the same
path with the panel mutex *not* involved at the LVGL level: the
synchronous ``lv_refr_now`` drains all 19 strips back-to-back through
the wrapper, and under ``s_rgb_mtx`` the moon image is stable, so no
strip can show a mixed frame.

The spin's direct write
-----------------------

The burn-in spin (:doc:`the animation pipelines <animations>`) displays at 8 fps. Its original path —
invalidate the moon image and let LVGL re-render the whole panel per
spin frame — cost ~175 ms of *touch-core* work per frame: at 8 fps
that is the LVGL task busy 100% of the time redrawing pixels that are
95% identical to the previous frame, and the spin fell to ~6 fps with
visible jitter. The fix moved the spin's frame **off LVGL entirely**:

.. code-block:: c
   :caption: components/ui/moon_ui.c (moon_spin_write_panel, S3 path)

   xSemaphoreTake(s_panel_mtx, portMAX_DELAY);
   if (CONFIG_TEST_SPIN_BANDS == 0) {
       for (int y = 0; y < MOON_PX; y++) {
           ...
           if (mrow[x]) row[x] = frow[x];   /* keep-mask pixels: front mirror */
       }
   }
   /* S3: ONE continuous write of the whole 400x400 window. The io
    * driver issues a single CASET+RASET + 0x2C and streams the 320 KB
    * with CS held active between 466x25x2 = 23,300 B chunks -- the bus
    * max_transfer_sz is set to exactly that size in the vendored BSP,
    * because the S3's largest DMA-capable internal-RAM block is ~31 KiB
    * (the io driver's default 32 KiB chunking NO_MEM'd the first chunk
    * of this write). The 16-strip variant (one CASET/RASET re-latch +
    * CS toggle per strip) left a visible seam line at every 25-row
    * strip boundary (2026-11-01). */
   (void)moon_panel_draw_wait(s_panel, MOON_IMG_POS, MOON_IMG_POS,
                              MOON_IMG_POS + MOON_PX, MOON_IMG_POS + MOON_PX, s_disp);
   xSemaphoreGive(s_panel_mtx);

The frame budget at 8 fps (125 ms/frame):

.. list-table::
   :header-rows: 1
   :widths: 30 18 52

   * - Step
     - Time (S3)
     - Note
   * - angle integrate + ``moon_rotate`` gather
     - ~8 ms
     - :doc:`rotation and the LUT <rotate-lut>`; 400×400 Q15.15 gather
   * - step-rotate into ``s_disp`` + keep-mask composite
     - ~4 ms
     - ``spin_step_rotate`` (the orientation step) + the mask loop
   * - panel write
     - ~30–40 ms
     - 320 KB over QSPI, async DMA after the staging copies;
       CPU is mostly idle
   * - *total*
     - *~45 ms*
     - *~80 ms of headroom per frame*

Two details of the S3 write are worth keeping. First, **one window,
not sixteen**: the 320 KB write is a *single* ``CASET`` + ``RASET`` +
``0x2C`` with the CS line held active between the io driver's 23,300-B
chunks. The earlier 16-strip variant (one window + CS toggle per
25-row strip) left a **visible seam line at every strip boundary** —
a QSPI panel artifact of the repeated window re-latch, not a data
error. The single-window form was measured 2026-11-01: zero errors
over the first spin frames, zero WDT/reboots over 120 s of sustained
spin.

Second, the **23,300-B ``max_transfer_sz``**: the io driver's default
chunking (32 KiB) would have staged a 32 KiB internal copy for the
first chunk of the 320 KB stream and NO_MEM'd — the largest
DMA-capable internal block is ~31 KiB. Setting the cap to exactly the
strip size makes the chunking match the staging budget.

The P4 writes differently (smaller strips: the panel and its PSRAM are
X16/XIP, so a large strip read starves the DPI controller's FIFO —
:doc:`the P4 port <p4-port>`), but the structure is identical: one atomic window per
frame, ``s_panel_mtx`` held for the whole write, the real
``draw_bitmap`` (not the wrapper) so the mutex is taken once.

The UI keep-mask
----------------

The spin writes the rotated moon *everywhere except where LVGL's own
UI lives* — the clock label (which sits *inside* the disk, so a radius
rule cannot separate moon from UI) and the seconds-arc ring outside
it. The mask is **per pixel**, one bit per moon-area pixel, built from
the analytic ring band plus a full-screen ``lv_snapshot`` with the
moon image hidden (the screen root is black everywhere, so every
non-black pixel *is* UI):

.. code-block:: c
   :caption: components/ui/moon_ui.c (mask rebuild, abridged)

   /* snapshot the active screen with the moon images hidden; every
    * non-black pixel is UI -> set its bit in s_ui_mask. The snapshot is
    * screen-local; the panel is rotated by the orientation step, so the
    * mask is re-rotated (spin_step_rotate's case logic) when the step
    * changes. Refreshed on: every screen switch, a clock/alarm label
    * change (the only content that changes shape while a moon screen
    * is active), and an orientation step change. */

The mask is refreshed (a) on every screen switch, (b) when the clock
or alarm label changes shape (the only UI content that changes while a
moon screen is active — the ring and knob never leave their band),
and (c) on an orientation-step change (the mask is screen-local; the
panel is rotated by the root transform, so the mask rotates with it).

The lost-DMA-completion freeze (and the watchdog)
-------------------------------------------------

The worst bug this project hit was a **silent, permanent, no-WDT
freeze** with a deterministic repro: a whole-panel LVGL flush in
flight while the spin's strip writes raced it. The root cause:

1. Every QSPI panel write — LVGL flush strip, spin frame, 0x51
   brightness — goes through the panel io's one ``spi_device`` and the
   GPSPI **bus lock** (a hardware-managed background request).
2. A queued DMA transfer's completion was *lost* (a CO5300 + QSPI
   driver failure mode on this board).
3. The bus lock's REQ/PEND bit was never cleared.
4. Every later ``spi_device_acquire_bus`` — LVGL's next flush, the
   spin's next strip, a 0x51 write — **blocked forever** in
   ``dev_wait`` (``portMAX_DELAY``), and the panel mutex was *held*
   for the duration.
5. Two tasks parked on one bus lock, no holder, no timeout: the
   Task WDT does not fire (parked tasks keep the idles alive), the
   screen just stops. The coredump showed the tell — ``lvgl`` and
   ``moon_spin`` both parked on the same bus-lock semaphore.

The fix is three layers:

.. code-block:: c
   :caption: components/ui/moon_ui.c (the watchdog, abridged)

   #define PANEL_IO_WATCHDOG_LIMIT_MS 3000
   static void moon_panel_watchdog_task(void *arg) {
       for (;;) {
           vTaskDelay(pdMS_TO_TICKS(PANEL_IO_WATCHDOG_LIMIT_MS));
           if (!s_panel_watchdog_armed)
               continue;
           TickType_t t0 = xTaskGetTickCount();
           BaseType_t r = xSemaphoreTake(s_panel_mtx, pdMS_TO_TICKS(PANEL_IO_WATCHDOG_LIMIT_MS));
           if (r == pdTRUE) {
               xSemaphoreGive(s_panel_mtx);
               continue;
           }
           /* Still held after 3 s of waiting, on top of 3 s of idle --
            * the holder is parked inside spi_device_acquire_bus on a
            * bus lock that will never clear. Restart: a clean reboot
            * instead of a dead screen. */
           ESP_LOGE(TAG, "PANEL IO STUCK: ... restarting");
           vTaskDelay(pdMS_TO_TICKS(100));
           esp_restart();
       }
   }

- **Layer 1 — serialization**: ``s_panel_mtx`` (the wrapper above) so
  no 0x51 or spin write is *queued behind* a strip that might never
  complete. A 0x51 write queued behind a lost-completion DMA parks
  *the ramp's writer* forever; the mutex prevents that interleave
  entirely.
- **Layer 2 — the fade hold**: ``s_fade_busy`` makes the spin **skip
  its panel write** while a 0x51 ramp is running (the spin keeps
  integrating its angle — the moon holds its pose for ~240 ms — and
  resumes from the exact angle when the ramp ends). The ramp's 0x51
  writes are then the *only* panel-io traffic in flight.
- **Layer 3 — the watchdog**: ``s_panel_mtx`` is held only for a strip
  (~10 ms) or a spin frame (30–80 ms) or a whole-panel flush (< 500
  ms). A test take that overruns 3 s *can only mean the bus is
  stuck* — no legitimate path waits that long — and the watchdog
  restarts the device. Priority 3 (above the spin and every UI task);
  the take is its only work, so it starves nothing.

The design philosophy: the bus lock is a *shared hardware resource
with a known failure mode*, so it gets (a) one owner at a time by
construction, (b) a bounded hold time, and (c) a watchdog that turns
the silent failure into a logged, recoverable reboot. The user sees a
~3 s pause and a reboot; without the watchdog they see a clock that
stops being a clock.

.. _display-lost-dma:

The wake-sync storm (the fourth ingredient)
-------------------------------------------

The three layers held for every field stall until the deterministic
repro that remained sat on the wake path: a bench hammer
(``TEST_AUTO_WAKE_SEC`` auto-wakes 5 s after the DEEP rung) stalled on
*every* tilt wake from deep sleep — ``PANEL IO STUCK`` 4.0–6.3 s into
the wake, panel watchdog, ``rst:0xc`` RTC_SW_CPU_RST, no panic. The
concurrent writer was neither the spin alone nor a 0x51 ramp: the
wake's on-demand WiFi sync runs ``esp_wifi_init`` in the sync task —
the full Wi-Fi firmware load into RAM plus RF calibration on the
prio-23 driver task — while the resumed spin and the 10 ms LVGL tick
flushes are live on the panel bus. That storm is what takes the DMA
completion. The corollaries discriminate it: boot runs the identical
storm *before* LVGL exists — never stalls; the alarm wake with the
moon paused stayed clean; and the 0x51-rides-alone guard on the
brightness API (the Layer-2 rule applied to every caller, 22c87ad)
changed nothing — proving the flush traffic, not the 0x51, was the
racing writer.

The fix extends the Layer-2 *skip* idea from the fade to the whole
storm: the **sync-storm panel-traffic hold**
(``moon_ui_panel_traffic_hold``). ``action_start_wifi_sync`` arms it
right before ``wifi_sync_kickoff`` (15 s self-healing cap; on the LVGL
task the arm additionally QUIESCES — the stub flush callback goes in
one tick early so the sync screen's own switch flush rides the bus
*before* the storm, and one in-flight DMA drains under the panel
mutex, because the hold flags cannot drop an already-queued strip);
``wifi_sync_task_fn`` first takes the radio fully DOWN
(``wifi_manager_deinit`` — see below) and only then releases it on both
outcomes (and the kickoff's task-create-failure branch does); the
release merely stamps a 4 s radio-tail drain that the UI-tick poll
then unfreezes at. While held, every panel
write is *dropped*: the spin's direct strip writes and the 0x51
brightness API are gated in ``moon_panel_draw_wait`` /
``moon_ui_panel_brightness_set``, the fade/dim ramps skip whole, and
the LVGL flushes drop at a **stub flush callback swapped into the
display** (``moon_ui_panel_repaint_pending``, polled on the UI tick —
the swap happens on the task that runs the flushes). The LVGL drop
point is the seam's one hard-learned constraint: dropping a flush
inside the panel ``draw_bitmap`` wrapper instead wedges the board —
the esp_lvgl_adapter's ``double_direct`` flush completes its LVGL hand
only through the real transaction's trans-done notify, and even its
error path blocks in the notify take, so a dropped flush leaves
``disp->flushing`` set and the LVGL task spins in
``wait_for_flushing`` forever (IDLE0 WDT spam, frozen screen — found
by the same proof hammer). Writers are never *blocked*: parking a
writer is the wake-freeze regression ``0c35dd7`` cured, and a dropped
writer simply advances — LVGL keeps drawing into its buffers, the spin
keeps integrating its angle. On release the UI tick restores the real
flush callback and invalidates the active screen: one whole-panel
repaint re-anchors panel, mirror and spin (the spin rewrites at 8 fps
regardless). The wake's own
brightness restore and the WiFi-screen dim ramp run *before* the hold
arms, so the transient screen switch keeps its normal traffic.

The hold alone was not the whole fix — the proof hammer caught two
more thieves in the same window. **The associated station keeps
stealing:** the first fixed build still stalled its very first cycle,
4 s *after* the release, because ``wifi_manager_init`` returns with
the STA still ASSOCIATED — a connected modem's ~300 ms DTIM beacon
wake takes a DMA completion exactly like the init storm does. The
airplane state therefore means modem-DOWN at the seam: the sync task
calls ``wifi_manager_deinit`` itself before stamping the tail, and
boot calls it right after the boot NTP path so the panel never wakes
up to a live radio. **The cycle itself leaked:** only the re-init
cleanup branch used to unregister ``wifi_event_handler``, so each
init/deinit cycle leaked two handler records into the default event
loop (duplicates firing N stray ``esp_wifi_connect`` calls per
STA_START) and halved the largest contiguous DMA block per cycle —
by cycle 10 ``esp_wifi_init`` NO_MEM'd and its ``ESP_ERROR_CHECK``
aborted the board. Deinit unregisters on every call, and
``esp_wifi_init`` failures return as logged errors instead of
aborting. **The teardown itself raced:** the last panic (LoadProhibited
in ``ieee80211_output_do``, EXCVADDR 0x18) was LWIP's DHCP RELEASE —
sent by ``esp_wifi_stop``'s netif-down on the tcpip thread — TX-ing
through a softmac the driver task was freeing underneath it. Deinit
now stops the DHCP client while the radio is still up (the release
goes out over a live ieee80211, the netif-down ``dhcp_stop`` has
nothing left to send) and lets the tcpip queue drain one 100 ms beat
before ``esp_wifi_deinit`` frees the softmac RAM.

Final proof (2026-11-12, the same hammer rig — auto-wake 125 s into
DEEP, 5-frame sweep, connected AP): **ten consecutive DEEP→wake cycles,
zero** ``PANEL IO STUCK``, WDT or panic lines. The largest
DMA-capable block settles from 31,744 B at boot to a stable 15,360 B
floor by wake 4 and stays there — comfortably above what the panel's
once-allocated strip staging needs (per-strip allocs do not hit the
heap), while the wifi-side allocs that once starved now fail
gracefully. Transient ~7.5 KB dips during a sync were shown to be
driver buffers that the DEEP window hands back.

Same code path on the P4 (protective — its wake storm is the remote
C6's init over SDIO); the knob's render seam stubs both APIs, since
its LEDC backlight has no shared bus lock to wedge.

The hold proved its worth the same week on a second member of the
class: the **settings WiFi scan** (2026-11-12, task #30 — "the clock
area is blank white, no digits, after leaving the WiFi password
screen"). The scan task's ``esp_wifi_init`` + the active scan are the
same completion-thief, and the bench repro (the ``TEST_SETTINGS_RETURN_SEC``
hook) logged 400 ``co5300`` write failures with two silently DROPPED
strips over exactly the clock row (y 176..214, y 214..252) in ~3 s.
Dropped strips are permanent there: LVGL answered ready for the held
writes, nothing re-invalidated them, and the spin's direct write only
ever covers the moon window — the clock row keeps whatever the last
full paint left in it. The password screen made it routine: leaving
it back to the AP list *restarts the scan*, and the exit-home then
lands the home switch's atomic flush inside the running storm.

The scan therefore borrows the sync hold through
``moon_ui_panel_traffic_hold_ext`` — but the hold **follows the scan,
not the screen**: ``settings_wifi_screen_refresh`` arms it before
``wifi_scan_begin`` (45 s cap, covering the 0-AP four-retry worst
case; the call is refcounted, so a list re-entry while an older hold
is live extends the cap instead of double-guarding), and the GENERIC
UI tick polls ``screens_wifi_scan_hold_poll`` — release fires with the
sync path's proven 4 s teardown tail only once the scan task is
really done, wherever the user went in the meantime. The release's
whole-screen repaint then heals everything the storm ate. Leaving the
screen mid-scan deliberately does NOT cut the guard — the first cut
of the fix released from the settings screen's own tick and from
``loadScreen``, which re-opened the drop exactly on the user's path
(atomic flush into the live storm, no pending repaint to heal it).
Bench proof (same hook, 2 s dwell so the home switch lands mid-scan):
hold ON 45000 → ``-> home`` fires held → scan completes unwatched →
release stamped 4000 ms tail → OFF with repaint queued — zero
``co5300``, zero dropped strips, no WDT, no hang-watch dump (the
hook's own earlier "LVGL task wedges" reading was traced to a
uint32 countdown underflow in the test hook, not to the panel path).

The class's fifth member sits *above* the seam, where no hold can
reach it: LVGL's own flush wait. ``wait_for_flushing`` is a
non-yielding ``while (disp->flushing);`` spin on whichever task enters
it — the LVGL task (its indev poll dies with the spin: touch goes
dead — the user's "touch is very unresponsive on the AMOLED",
task #31) or the sweep task at ``lv_refr_now``. Normally unreachable,
the adapter keeps up — but a **lost trans-done hand** makes it
permanent: ``disp->flushing`` was already set when the hold swaps its
stub in (the sync screen's own pre-arm switch flush loses the hand to
the very radio session the hold was armed for), the notify never
lands, and no mutex is held — the task WDT only prints and the panel
watchdog is blind. The seam's answer is ``moon_flush_wait_cb``,
installed at ``moon_ui_init`` via ``lv_display_set_flush_wait_cb``:
it gates on what actually matters instead of on the LVGL hand — every
panel writer (adapter flushes, spin strips, atomics) serializes under
``s_panel_mtx`` and ``moon_panel_draw_wait`` consumes the trans-done
*inside* it, so a free mutex means the flush physically landed.
Returning makes LVGL clear ``disp->flushing`` itself and the refr pass
continues into ``flush_cb`` — under a hold that is the stub, so the
screen self-heals exactly as the hold design intends; the 2 s cap
only fires on a genuinely stuck bus, where it logs and lets LVGL
proceed. The healthy path never runs the callback:
``wait_for_flushing`` calls it only while ``flushing`` is still set.
Shared code — S3 and P4 alike; the knob's render seam has no shared
bus lock to wedge.

The 0x51 brightness path
------------------------

The CO5300 has its own hardware brightness (command 0x51, 0–100%),
independent of the pixels. Three uses: the screen-switch fade
(:doc:`the animation pipelines <animations>`), the light-sleep dim (the panel ramps to
``UI_LIGHT_SLEEP_DIM_PCT`` = 25% over the fade duration, :doc:`the clock and sleep machine <clock-sleep>`),
and the deep-sleep backlight off. Every write funnels through one
serialized entry point:

.. code-block:: c
   :caption: components/ui/moon_ui.c (moon_ui_panel_brightness_set)

   esp_err_t moon_ui_panel_brightness_set(int pct) {
       esp_err_t r;
       if (s_ready && s_panel_mtx) {
           xSemaphoreTake(s_panel_mtx, portMAX_DELAY);
           r = bsp_display_brightness_set(pct);
           xSemaphoreGive(s_panel_mtx);
       } else {
           r = bsp_display_brightness_set(pct);
       }
       if (r == ESP_OK)
           s_panel_pct = pct; /* the fade/dim ramps start from the ACTUAL level */
       return r;
   }

The ``s_panel_pct`` tracking is what keeps the ramps honest: the
fade-out ramps *from the actual level* (after a light-sleep dim the
panel sits at 25%, and a hard-coded 100→0 ramp would flash the screen
brighter before dimming it), and the dim ramp is a no-op when the
panel is already at its target.

Panel byte order
----------------

One last hardware fact: the QSPI io sends 16-bit pixels **MSB-first**,
which is the *byte-swapped* form of LVGL's native RGB565. The LVGL
flush path is pre-swapped by the adapter's bridge (``lv_draw_sw_rgb565_swap``
fires for this panel interface), so LVGL's strips arrive at the panel
correct. The spin's *direct* write bypasses the adapter's swap, so
``spin_step_rotate`` byte-swaps every pixel on the way out:

.. code-block:: c
   :caption: components/ui/moon_ui.c

   #if defined(CONFIG_BOARD_P4)
   static inline uint16_t spin_bswap16(uint16_t v) { return v; }
   #else
   static inline uint16_t spin_bswap16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
   #endif

The P4 board's (Waveshare ESP32-P4-WIFI6-Touch-LCD-XC) MIPI-DSI
panel wants LVGL-native order (its adapter bridge
does not swap — ``lv_draw_sw_rgb565_swap`` fires only for the
"OTHER" panel interface), so the identity is correct there. Getting
this wrong is invisible in the simulator (the host renderer writes
PNGs) and shows up on the panel as every color's red and blue
channels transposed — the "the moon looks teal" bug class.

:doc:`The animation pipelines <animations>` assembles these pieces into the motion: the boot sweep,
the free spin, the dusk, and the fade.
