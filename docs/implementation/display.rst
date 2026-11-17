The display pipeline
====================

This page is the one a moon clock's design is actually made of:
a 466×466 RGB565 QSPI panel driven by a dual-core CPU whose display
DMA can move **32 KiB at a time**, whose PSRAM is **not DMA-capable**,
and whose panel io can, on rare occasions, **lose a DMA completion and
hang the bus forever**. Every mechanism on this page — the 25-row
strips, the 23,300-byte transfer cap, the panel mutex, the watchdog —
is a consequence of those three facts.

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
       /* No hold gate HERE: a vtable caller (the adapter's blit) expects the
        * transaction's trans-done notify, so a drop at this seam breaks its
        * hand. LVGL flushes drop at hold_flush_cb instead; during a hold
        * nothing reaches this wrapper except the switch-frame pass
        * (s_hold_pass_switch -- screen_switched's atomic window). */
       xSemaphoreTake(s_panel_mtx, portMAX_DELAY);
       esp_err_t r = moon_panel_draw_bitmap_locked(panel, x_start, y_start, x_end, y_end,
                                                   color_data, false);
       xSemaphoreGive(s_panel_mtx);
       return r;
   }

The wrapper's job is **serialization** — every LVGL flush strip takes
``s_panel_mtx``, the same mutex the 0x51 brightness writes take, and
rides it into ``moon_panel_draw_wait`` (the accepted/rejected retry,
below). The panel io's own bus lock serializes the actual transfers;
the mutex is what keeps *this* project's code from queueing a 0x51
behind a strip that might never complete (the freeze, below). (Until
2026-11-13 the wrapper had a *second* job — refreshing the front
mirror ``s_front`` so the spin's direct-write composite could keep the
clock and arc in place; retired with the spin subsystem, below.)

The atomic whole-panel flush (``moon_ui_screen_switched``) is the same
path with the panel mutex *not* involved at the LVGL level: the
synchronous ``lv_refr_now`` drains all 19 strips back-to-back through
the wrapper, and under ``s_rgb_mtx`` the moon image is stable, so no
strip can show a mixed frame.

The strip pacing and the staging queue, both boards side by side —
the S3's 25-row strips exist because its 16 MB PSRAM is not
SPI-DMA-capable (every strip is staged through internal RAM and the
466-wide 25-row buffer is 23,300 B, under the 32 KiB SPI-DMA cap, so
queue depth 1 means one strip = one buffer = one QSPI transfer); the
P4's 130-row pacing is a *choice* — MIPI-DSI has no SPI-DMA ceiling,
and the flush simply rides the XC BSP's 50-row DPI-DMA buffer
(``docs/tools/gen_strip_staging.py`` draws this figure from the same
constants the code uses):

.. figure:: ../_static/figs/strip_staging.png
   :alt: Strip staging, S3 vs P4 — the 400 px disk split into 16 25-row strips with a 25-row in-flight buffer and 2 queued (23.3 KiB under the 32 KiB SPI-DMA cap, queue depth 1), and the 650 px P4 disk into 5 130-row strips riding three 50-row MIPI buffers
   :align: center
   :width: 100%

   The moon disk's rows are the only pixels that stage; the black
   surround never enters a strip.

What these transactions weigh on each bus — bytes per refresh against
the 32 KiB SPI-DMA cap and the raw wire time one full panel costs the
QSPI x4 wire against the 33 ms beat — is charted as the bus ladder in
:doc:`the performance page <../verification/performance>`.

The whole path, first sealed and then opened — who may produce a panel
state (the atomic rule and its two sanctioned exceptions), how a strip
travels (PSRAM draw buffer, byte-order swap, the vtable wrapper,
25-row DMA staging), and the three witnesses on ``s_panel_mtx`` (the
2 s flush-wait gate, the 3 s watchdog, the drain verify with its
in-place-heal versus budgeted-restart split) — drawn from the code:

.. graphviz:: ../_graphs/flush-path.dot
   :alt: The flush path overview — rule, exceptions, draw path, BSP, witnesses
   :align: center
   :caption: The overview — the rule: every moon-pipeline frame hits the panel as ONE atomic whole-panel ``lv_refr_now`` under ``s_rgb_mtx`` (``moon_ui_screen_switched``), never a half-old/half-new composite. The two exceptions: the 0x51 hardware brightness ramp that writes NO pixels (the fade-to-black screen switch ``UI_SCREEN_SWITCH_FADE`` and the once-per-hour refresh ``UI_MOON_HOURLY_ENABLE``, whose producer rebuilds off-panel while black — the new frame's ONE atomic flush lands while the panel is still black, then fades back) and the bake reveal (``UI_MOON_BAKE_REVEAL`` / ``KNOB_UI_BAKE_REVEAL``: a computing frame shows as designed CONSISTENT STATES — black canvas → one completed strip band per 50 ms tick → healed old frame on any abort; each state is itself ONE atomic band under ``s_rgb_mtx`` WITH the drain witness, unbaked rows are black, ``s_frame`` is never touched mid-reveal).

.. graphviz:: ../_graphs/flush-path-strip.dot
   :alt: One strip's journey from PSRAM to panel
   :align: center
   :caption: One strip — the buffer is octal PSRAM (``esp_ptr_dma_capable`` false below ``SOC_DMA_LOW``, so nothing flies unstaged); the ``ESP_LV_ADAPTER_PANEL_IF_OTHER`` bridge runs ``lv_draw_sw_rgb565_swap`` so bytes arrive in panel order; the vtable wrapper takes LVGL flushes, drain probes and 0x51 ramp steps alike under ``s_panel_mtx``, its accept retry absorbing the P4 DPI panel's async-DMA2D ``ESP_ERR_INVALID_STATE`` rejects (the S3's QSPI writes synchronously, first attempt wins); ``buffer_height`` 25 rows = 466×25×2 = 23.3 KiB — a 50-row strip's 32 KiB first chunk exceeds the largest DMA-capable internal block (~31 KiB) and EVERY full-panel flush fails ``ESP_ERR_NO_MEM`` (the 50-row failure: hardware invariant, not tuning); staging obeys the 32 KiB ``SPI_LL_DMA_MAX_BIT_LEN`` cap and the vendored BSP's ``max_transfer_sz`` 23,300 B; queue DEPTH 1, because the depth-1 ``trans_pool[0]`` descriptor carries the per-transaction completion flag — the very slot the lost-completion family recycles under the ISR.

.. graphviz:: ../_graphs/flush-path-witness.dot
   :alt: The three witnesses on s_panel_mtx
   :align: center
   :caption: The witnesses — a legitimate hold is a strip ~10 ms or a whole-panel flush <500 ms, so the 3000 ms watchdog limit can ONLY be crossed when the bus lock's REQ/PEND bit was never cleared: a lost DMA completion freezes ``dev_wait`` at ``portMAX_DELAY`` with no mutex holder, silent and WDT-blind, and the watchdog trades the dead screen for a logged clean reboot. The drain verify (S3/knob, ``#if !P4``) holds ``s_panel_mtx`` across the WHOLE submit+witness window — no call site holds it, audited 2026-11-13 — with a last-row drain write, a 20 ms lead-in (a strip DMAs in ~10 ms) and a witness on ``s_panel_done_cnt``: post-flush probes swallow deterministically, so the pacing build grants ONE 30 ms grace and heals straight away at ~50 ms, a fraction of the 3 s + 3 s (the pre-pacing ladder's two 300 ms poll windows — a de-facto ~640 ms per verified flush — are retired). Accepted-but-silent means the ISR's ``en_trans_done_cb`` flag was EATEN (stale ``ret_queue`` double-post → inflight bookkeeping runs ahead → unconditional ``pool[0]`` memset) — a no-op ``tx_param(-1)`` runs the driver's own drain loop and re-arms the queue IN PLACE; not even accepted means a wedged queue and the budgeted site restart ``drain_recovery``.

Who hands the pixels *to* this path — which leg fills which buffer and
under which key before a strip ever reaches the wrapper — is the relay
in ``docs/_graphs/buffers-relay.dot``
(:doc:`the architecture <architecture>`); the strip staging and its
witnesses stay here. The same three witnesses counted against every
other guard the app owns — two keys on the clocks, one
reentrant-wrapped key on the knob, zero queues anywhere — are
:ref:`the guard ladder <guard-ladder-perf>`, and the whole guard
family drawn as a node graph is :ref:`the guards sketch <guards-arch>`.

The direct-write spin (history, removed 2026-11-13)
----------------------------------------------------

Until 2026-11-13 the home's moon rotated on the panel by itself. The
burn-in spin displayed a once-baked frame at 8 fps by **direct**
``esp_lcd_panel_draw_bitmap`` writes under ``s_panel_mtx``, bypassing
LVGL entirely (the second sanctioned exception). Its original LVGL
path had cost ~175 ms of *touch-core* work per frame — the LVGL task
100% busy redrawing pixels 95% identical to the previous frame, the
spin falling to ~6 fps with visible jitter — so the write moved off
LVGL, and a per-pixel UI keep-mask (analytic ring band + a full-screen
``lv_snapshot`` with the moon hidden) plus a front mirror ``s_front``
refreshed by the wrapper kept the clock label and seconds arc legible
over the rotating disk.

Two hardware lessons from that era still constrain the panel path:

- **One window, not sixteen.** The spin's 320 KB whole-disk write was
  a single ``CASET`` + ``RASET`` + ``0x2C`` with the CS line held
  active between the io driver's 23,300-B chunks; the earlier 16-strip
  variant (one window re-latch + CS toggle per 25-row strip) left a
  **visible seam line at every strip boundary** — a QSPI artifact of
  the repeated re-latch, not a data error (measured clean 2026-11-01:
  zero errors, zero WDT/reboots over 120 s of sustained spin). And the
  S3's largest DMA-capable internal block is ~31 KiB, which is why the
  vendored BSP's ``max_transfer_sz`` stays pinned to exactly the
  23,300-B strip size — a 32 KiB first chunk would NO_MEM the staging
  copy. The LVGL flush path rides that same cap today.
- **The snapshot can starve.** A keep-mask rebuild is a ~434 KB
  full-screen ``lv_snapshot`` from the PSRAM pool; a live TF codec
  scratch (~1.1 MB of gray-JPEG buffers) could squeeze it out, the
  rebuild kept ``s_mask_dirty`` and retried on the tilt monitor's 1 s
  tick, and a stale mirror pasted old clock digits between the fresh
  flushes (task #39, 2026-11-13 — the sweep returning the codec
  scratch after its last frame was that fix).

The subsystem — keep-mask, mirror, step-rotate, byte-swap helper,
direct writes — was removed in its entirety later the same day at the
user's request (*"It does more things worse than good."*). The home
moon is plain LVGL, shown static; every moon frame now rides the
atomic ``lv_refr_now`` path and the wrapper's second job is gone.

The lost-DMA-completion freeze (and the watchdog)
-------------------------------------------------

The worst bug this project hit was a **silent, permanent, no-WDT
freeze** with a deterministic repro: a whole-panel LVGL flush in
flight while the spin's strip writes raced it. The root cause:

1. Every QSPI panel write — LVGL flush strip, drain probe, 0x51
   brightness — goes through the panel io's one ``spi_device`` and the
   GPSPI **bus lock** (a hardware-managed background request).
2. A queued DMA transfer's completion was *lost* (a CO5300 + QSPI
   driver failure mode on this board).
3. The bus lock's REQ/PEND bit was never cleared.
4. Every later ``spi_device_acquire_bus`` — LVGL's next flush, a 0x51
   write — **blocked forever** in ``dev_wait`` (``portMAX_DELAY``),
   and the panel mutex was *held*
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
           /* Still held after 3 s of waiting, on top of 3 s of idle.
            * First ask the holder what it waits for: a strip riding
            * out the storm's internal-RAM squeeze (every rejected
            * attempt was NO_MEM -- see the sixth member below) is NOT
            * a wedged bus; LOG and keep watching. Anything else -- the
            * holder parked inside spi_device_acquire_bus on a bus lock
            * that will never clear -- restarts: a clean reboot
            * instead of a dead screen. */
           if (s_panel_no_mem) {
               ESP_LOGW(TAG, "panel mutex held ... riding out a no-mem strip "
                            "bounce (storm pressure, bus healthy) -- not restarting");
               continue;
           }
           ESP_LOGE(TAG, "PANEL IO STUCK: ... restarting");
           vTaskDelay(pdMS_TO_TICKS(100));
           esp_restart();
       }
   }

- **Layer 1 — serialization**: ``s_panel_mtx`` (the wrapper above) so
  no 0x51 write is *queued behind* a strip that might never
  complete. A 0x51 write queued behind a lost-completion DMA parks
  *the ramp's writer* forever; the mutex prevents that interleave
  entirely.
- **Layer 2 — the fade hold**: ``s_fade_busy`` makes every other
  brightness writer stand down while a 0x51 ramp is running (the ramp
  runs on the panel mutex step by step, and
  ``moon_ui_panel_brightness_set`` joins the same flag — so the ramp's
  0x51 writes are the *only* panel-io brightness traffic in flight).
  In the spin's era this layer also froze the spin's panel write for
  the ramp's ~240 ms; with the spin gone the flag's whole job is
  making 0x51 ramps mutually exclusive and keeping other brightness
  writers off the ramp's window.
- **Layer 3 — the watchdog**: ``s_panel_mtx`` is held only for a strip
  (~10 ms), a 0x51 ramp step, or a whole-panel flush (< 500
  ms). A test take that overruns 3 s *can only mean the bus is
  stuck* — no legitimate path waits that long — and the watchdog
  restarts the device. Priority 3 (above every UI task);
  the take is its only work, so it starves nothing. One sanctioned
  exception (the sixth member): while the holder is a flush strip
  riding out a storm-pressure ``ESP_ERR_NO_MEM`` (``s_panel_no_mem``),
  the bus is provably healthy and the watchdog logs instead of
  restarting.

The design philosophy: the bus lock is a *shared hardware resource
with a known failure mode*, so it gets (a) one owner at a time by
construction, (b) a bounded hold time, and (c) a watchdog that turns
the silent failure into a logged, recoverable reboot. The user sees a
~3 s pause and a reboot; without the watchdog they see a clock that
stops being a clock. All three layers — and the drain verify with its
in-place-heal versus budgeted-restart split, which closed this family
after the watchdog had already ruled out the simple wedge — are the
witness cluster of the whole-path picture above
(``docs/_graphs/flush-path.dot``).

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
write is *dropped*: the 0x51 brightness API is gated in
``moon_ui_panel_brightness_set``, the fade/dim ramps skip whole, and
the LVGL flushes drop at a **stub flush callback swapped into the
display** (``moon_ui_panel_repaint_pending``, polled on the UI tick —
the swap happens on the task that runs the flushes). The one
exception is the **switch-frame pass** (user 2026-11-16: "cannot
enter settings from the wifi screen with touch" — bench RED 3/3: the
tap opened the hub in LVGL in 20 ms, the panel never showed it until
the release repaint, 7 s later): a screen switch is user presence, so
``moon_ui_screen_switched`` brackets both atomic ``lv_refr_now`` legs
with ``s_hold_pass_switch`` and the switch frame is exempt at BOTH
drop seams — ``hold_flush_cb`` forwards its strips (``flush PASSED
(hold)``) and ``moon_panel_draw_wait`` submits them through the live
hold. The periodic repaints (seconds arc, clock row) stay dropped and
self-heal at the release repaint as designed; the passed frame rides
the storm bus exactly as the 0x51 ramp steps do, with the drain
witness and panel watchdog owning a lost completion — and the
watchdog's no-mem exception (the sixth member below) owning the
storm's memory squeeze on that same passed frame. The LVGL drop
point is the seam's one hard-learned constraint: dropping a flush
inside the panel ``draw_bitmap`` wrapper instead wedges the board —
the esp_lvgl_adapter's ``double_direct`` flush completes its LVGL hand
only through the real transaction's trans-done notify, and even its
error path blocks in the notify take, so a dropped flush leaves
``disp->flushing`` set and the LVGL task spins in
``wait_for_flushing`` forever (IDLE0 WDT spam, frozen screen — found
by the same proof hammer). Writers are never *blocked*: parking a
writer is the wake-freeze regression ``0c35dd7`` cured, and a dropped
writer simply advances — LVGL keeps drawing into its buffers. On release the UI tick restores the real
flush callback and invalidates the active screen: one whole-panel
repaint re-anchors panel and screen. The wake's own
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

The lifecycle, first sealed and then opened at each end — arm on the
kickoff, release on the radio session, the screen itself touching
neither end:

.. graphviz:: ../_graphs/scan-panel-hold.dot
   :alt: The scan hold lifecycle overview
   :align: center
   :caption: The overview — the settings screen ARMS the refcounted hold (45 s cap, re-entry extends rather than double-guarding) and starts the scan task, the completion-thief; while held, panel writers are stubbed and ``flush_cb`` answers READY; the generic ui tick owns the release, and the release repaints the whole screen, wherever the user went meanwhile.

.. graphviz:: ../_graphs/scan-panel-hold-arm.dot
   :alt: The scan hold arm side
   :align: center
   :caption: Arming — ``settings_wifi_screen_refresh`` calls ``scan_panel_hold_arm()`` BEFORE ``wifi_scan_begin``, because the storm starts when the scan task reaches ``esp_wifi_init``; leaving mid-scan deliberately does NOT cut the guard, since the user's path lands the home switch's atomic flush INSIDE the running storm (the first cut of the fix released from the screen's own tick and from ``loadScreen`` and re-opened the drop exactly there). Bench repro 2026-11-12: ~400 ``co5300`` write failures plus 2 silently DROPPED strips over the clock row (y 176..214 / 214..252) in ~3 s.

.. graphviz:: ../_graphs/scan-panel-hold-release.dot
   :alt: The scan hold release side
   :align: center
   :caption: Releasing — ``screens_wifi_scan_hold_poll`` is registered BEFORE the repaint poll so one tick sees the release stamp; the guard drops only when ``wifi_scan_in_progress`` is false, with the sync path's proven 4 s ``esp_wifi_stop``/deinit teardown tail; the release queues ``moon_ui_panel_repaint_pending`` → ``lv_obj_invalidate(active)``, healing every strip the storm ate.

The class's fifth member sits *above* the seam, where no hold can
reach it: LVGL's own flush wait. ``wait_for_flushing`` is a
non-yielding ``while (disp->flushing);`` spin on whichever task enters
it — the LVGL task (its indev poll dies with the spin: touch goes
dead — the user's "touch is very unresponsive on the AMOLED",
task #31) — and, until the boot sweep retired, the sweep task at
``lv_refr_now``. Normally unreachable,
the adapter keeps up — but a **lost trans-done hand** makes it
permanent: ``disp->flushing`` was already set when the hold swaps its
stub in (the sync screen's own pre-arm switch flush loses the hand to
the very radio session the hold was armed for), the notify never
lands, and no mutex is held — the task WDT only prints and the panel
watchdog is blind. The seam's answer is ``moon_flush_wait_cb``,
installed at ``moon_ui_init`` via ``lv_display_set_flush_wait_cb``:
it gates on what actually matters instead of on the LVGL hand — every
panel writer (adapter flushes, atomic displays, drain probes) serializes under
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

The class's sixth member is neither a stolen hand nor a wedge — it is
an allocation the storm **refuses**: the strip's own DMA staging.
Every PSRAM strip's transaction bounces through a contiguous 23,300 B
internal-RAM block (``setup_priv_desc``; the S3's PSRAM is not
SPI-DMA-capable), and ``esp_wifi_init`` can hold the internal heap
below that size. Bench 2026-11-17 (user: "pressing x on the settings
crashed it" — exit settings into the sync screen, touch mid-sync): the
switch-frame pass did its job and forwarded the user's frame under the
hold, but the radio storm had the largest free DMA block at 20,480 B,
so every submission of every strip was rejected at the door — the
``moon_panel_draw_wait`` tally read ``io errors: 100×0x101``
(``ESP_ERR_NO_MEM``), ``largest-DMA 20480 B`` — while the bus itself
was provably healthy: each attempt's tiny CASET/RASET polling writes
completed, only the bounce died. Each strip then burned ~1.5–2 s of
the classic 100-attempt cap holding ``s_panel_mtx``, the watchdog's
test take overran 3 s on top, and the board restarted with a false
lost-completion diagnosis.

A wedge and a squeeze are not the same failure, so the seam now tells
them apart by the error code itself. ``moon_panel_draw_wait`` sets
``s_panel_no_mem`` while a rejected strip's attempts are all
``ESP_ERR_NO_MEM``, and the strip then **rides the storm out** instead
of burning the 100-attempt cap: up to ``PANEL_NO_MEM_RETRY_MS``
(8000 — the measured init/calibrate/connect/teardown window runs
5–8 s), rate-limited to one ``strip bounce no-mem (storm pressure)
... riding out`` line per 2 s with the live largest-DMA witness. The
flag simultaneously tells the watchdog the holder is healthy: the
multi-second hold that used to end in ``PANEL IO STUCK`` now logs
``riding out a no-mem strip bounce ... not restarting`` and keeps
watching. The ride-out cannot chain across a whole panel: at most
``PANEL_NO_MEM_GIVEUP_CAP`` (3) strips may spend the 8 s, any
non-NO_MEM error clears the flag at once (a lost hand stays a lost
hand — the drain witness, in-place heal and budgeted
``drain_recovery`` own it), and past either bound the classic cap
returns TIMEOUT and the watchdog resumes normal ownership — the
fourth NO_MEM give-up restarts exactly as before. The next accepted
strip zeroes the give-up budget, so one storm cannot tax a later one.
Bench (fix build, 20-min re-capture of the boot-sync storm):
``strip bounce no-mem (storm pressure): task lvgl (y 0..24),
largest-DMA 9728 B -- riding out`` at 19.4 s — accepted without a
give-up, no ``PANEL IO STUCK``, the drain verify healed the swallowed
completion at 53 s and the clock ran on. Shared code — S3 and P4
alike.

The class's sixth member was caught on the **light-sleep dusk
replay** (the dusk itself was retired 2026-11-13; the discovery bench,
2026-11-13: "the S3 AMOLED crashed coming back from light sleep via
touch"):
``moon_tick_cb`` displays each published dusk frame with the tick's
atomic ``lv_refr_now`` while the dusk task bakes the next frame on
core 1 — and ``lv_refr_now`` returns with the frame's LAST strip's DMA
still in flight. When that one trans-done is lost (same driver failure
mode; on the bench it surfaced right after an atomic whole-panel
display — a dusk frame in the first two ``TEST_AUTO_WAKE_SEC`` hammer
captures, later a boot-sweep last frame and a scan-hold release
repaint), the depth-1 flush queue stays occupied
invisibly — nothing touches the bus until the user's wake runs its
atomic pose flush into the occupied slot. The capture reads cleanly:
last dusk strip enqueued, ~700 ms of quiet, then every wake strip is
rejected on attempt 0 ("spi transmit (queue) color failed", task
``lvgl`` per the wrapper's attribution probe), each strip burning
~2 s of the 100-attempt retry loop, and the panel watchdog restarts
the board (``PANEL IO STUCK ... by task lvgl``, ``rst:0xc``) — a
~4.5 s freeze into the user's face, then a reboot that reads as a
crash.

The fix applies the class's own logic one step earlier: every
sanctioned atomic whole-panel flush is followed by a
**drain-verify** — ``moon_panel_drain_verify(site)`` waits 20 ms (a
strip DMAs in ~10 ms, so the healthy path is an attempt-0 accept) and
writes one black **full-width row** at the panel's bottom edge via the
real ``s_panel`` handle (``y 465``: DRAM, 466 px, 932 B, one DMA chunk
— the exact strip shape; bottom edge: outside every widget, black on
AMOLED black; passing ``NULL`` instead
dereferences the CO5300 panel struct inside ``s_orig_draw`` — a
core-0 ``LoadProhibited`` the first probe build learned the hard way).
The row's shape is not cosmetic: the original probe was ONE 2-byte
pixel from a ``static const`` FLASH (DROM) buffer — the only color
write on the whole bus that ``spi_master`` heap-bounces per write, and
the only write whose io callback never fired at ANY site in ANY boot
(capture 7: strips 19/19 per frame, probe 0/2). Every earlier
capture's "probe accepted" was genuine while its own completion
silently never existed — which is why the witness below demanded its
probe's own done, and why the probe had to become a DMA-capable
single-chunk row first.
The write forces the pending trans-done to be consumed NOW, at the
flush site; if it does not come back (``moon_panel_draw_wait`` rejects
for ~2 s), the log names the SITE and the same recovery the watchdog
would run seconds later — into the next writer's freeze — happens
there instead, in the dark where it is invisible. The bench
proved the probe cannot live on one path alone: the next
capture's first boot lost the BOOT SWEEP's last-frame hand (the
spin's first window write rejected on attempt 0, 120 ms after
``sweep 5/5 shown``) and then lost another after the scan-hold
release repaint, with the watchdog naming task ``moon_spin`` — all
those captures predate the spin's removal (and the sweep's and the
dusk's, later the same
day). So the
helper runs after every atomic display: the orbit
batch, the wake's static pose display,
the deferred re-bake display, the hourly refresh's while-black show
(``drain_verify("hourly fade-in")``), and the screen-switch flush with its fade-in leg.
S3 only (the DPI P4 rejects legitimately and needs no such probe).

The single probe still left one member uncaught, and the next capture
walked into it: a probe's accept proves the *previous* write (a
depth-1 queue only accepts when empty) but can never prove its
**own** completion — and no chain of probes closes that tail, because
the next writer that would prove a tail pixel is itself the frozen
victim. The dusk-replay probe was accepted ~700 ms before the wake;
the wake-join burst — the deferred re-bake task starting on core 1,
the cache-invalidation storm around it — then ate the probe pixel's
trans-done, and the wake flush's strip 1 met the occupied queue: the
same 2 s-per-strip freeze, one write later, ``last accepted: lvgl
y465..466 @ 1500 ms ago (the culprit)``, three boots, one signature.

The proof therefore comes from the driver itself, from the very
completion whose loss starts the family: the io's
``on_color_trans_done``. It is a single slot and the
``esp_lvgl_adapter`` bridge (``PANEL_IF_OTHER``'s default branch)
owns it from ``bsp_display_start`` — and in the S3's ``NONE``
tear-avoid mode that very handler *is* the display's flush-ready
signal (``display_bridge_v9_handle_vsync`` →
``display_manager_flush_ready`` → ``lv_display_flush_ready``;
dummy-draw off, no TE), so replacing it wedges LVGL's own refr loop.
``moon_ui_init`` runs after the adapter, so the witness **chains**
(``moon_panel_witness_arm``): it reads the bridge's ``{fn, ctx}`` off
the io's private struct — those two callback fields are pinned to the
IDF v5.5.1 ``esp_lcd_panel_io_spi.c`` layout, and drift shows up LOUD,
never silently: the boot-time arm test (one drain-ROW write, its OWN
increment required, under the same serialized discipline as the drain
— capture 6 had proved the old "any ambient increment counts" test
worthless) either sees no increment and leaves the drain accept-only,
or faults at boot on the bench — registers its own
counting callback through the official single-slot API, and forwards
every completion untouched into the bridge's handler. The witness
counts every color completion on the single QSPI spi_device.

With the counter the drain can prove its probe's own done — but the
first witnessed builds (captures 6–9) turned the witness into a
deterministic false-positive: probes were accepted while their counter
sat dead at exactly the sites that share the bus with other writers
(the spin park during the alarm-ring storm, the switch fade-in, the
screen switch), and never at the queue-exclusive ones (the sweep
frames, 19/19 completions per frame). The RTC-FAST mirror + throttled
ISR-side log settled it (capture 9): at a frozen site the **callback
side was dead too** — poller cache-blindness was ruled out. The
mechanism is the io driver's per-transaction completion flag
(``en_trans_done_cb``): it lives inside the depth-1 pool *descriptor*,
and the next writer ``memset``s that slot before reuse
(``esp_lcd_panel_io_spi.c``); a concurrent writer slipping in during
the probe's ~10 ms flight — a fade-ramp 0x51 ``tx_param``, an LVGL
strip, a spin strip — recycles the descriptor under the ISR and
silently eats the flag. The accept stays genuine (the recycle path
desyncs the io's ``num_trans_inflight`` bookkeeping by one), the
probe's own callback never fires, and the following writer is fine.
That is the impossible triangle — accept ⇒ ISR ran ⇒ callback chained
⇒ increment, yet none — resolved.

The final drain therefore enforces three rules, each bought with a
capture: **shape** — a DMA-capable DRAM single-chunk row (above);
**hold** — under a live panel hold the preceding flush was itself
dropped (``hold_flush_cb`` / ``moon_panel_draw_wait``'s shortcut never
touched the bus), so the drain no-ops entirely (probing anyway would
ride the shortcut into a phantom accept — the capture-8 hold series);
the switch-frame pass is exempt (amended 2026-11-16 with the pass:
a forwarded strip sets ``s_hold_pass_pending``, its trans-done hand is
REAL, and the witness runs for it — the bench caught the sync screen's
own pre-arm wedge healing exactly there);
probes that start healthy submit **strict**, bypassing the shortcut,
so a hold beginning mid-drain can never phantom-accept; and
**serialization** — the drain holds ``s_panel_mtx`` across the whole
submit+witness window (no drain call site holds it — audited), making
the probe's transaction the only io traffic while its completion is
pending. One probe, one 30 ms grace, then the heal — a measured ~53 ms
hold (bench 2026-11-15), a fraction of the panel watchdog's 3 s + 3 s.
With the queue exclusive, the depth-1 FIFO argument holds: the
snapshot after the accept means only the probe is in flight, and an
increment proves its own done. The bench truth is that the increment
never comes behind a multi-strip flush — every post-flush probe loses
its own completion to the recycle race — so the heal is the NORMAL
path, and the witness was rebuilt around that (a two-probe ladder with
two 300 ms poll windows once billed ~641 ms per verified flush; the
trace that caught it is in :doc:`the bake reveal <bake-reveal>`).
A merely-LATE completion is reaped by the heal's own drain loop, at
worst logging a false "swallowed". Serialization still does not PREVENT the loss — it
makes the witness airtight, which is what lets the two shapes be
treated differently: a probe that cannot even be accepted is a WEDGED
queue → restart at the site, with the panel still dark, before the
next writer learns, guarded by a per-boot restart budget in
``RTC_NOINIT`` (magic-checked, 3 restarts: a regression that turns the
witness into a boot loop degrades to a working clock instead of a
restart storm); an accepted probe whose counter stays frozen is the
driver having eaten the completion flag (the GPSPI ISR has no
``host->polling`` guard — a completion interrupt still enabled from
the strip stream can deliver a co5300 per-draw polling transaction's
pointer into the display device's ret_queue, desyncing the io
driver's ``num_trans_inflight`` so its unconditional pool[0] recycle
clears the in-flight transaction's ``en_trans_done_cb`` — see the
verification page's capture-6..10 record) → heal IN PLACE: a no-op
``esp_lcd_panel_io_tx_param(io, -1, NULL, 0)`` runs the driver's own
drain loop over all inflight entries and re-arms the flush queue,
with the site and slot-ownership evidence logged (``slot MINE``
guards against a stolen single callback slot). A truly undelivered
completion parks that call in ``get_trans_result`` under the panel
mutex and the watchdog restarts in 3 s + 3 s — the honest fallback
for a wedge.
While the dusk existed its wake branch additionally probed BEFORE
starting the pose flush (``wake preflush``) — whatever the last
accepted write was, a loss surfaced at the flush site in the dark,
never as the freeze-then-crash in the user's face; the plain static
wake display carries the class's normal post-flush probe today.
Healthy cost: one row write and ~10 ms per probe
site; the de-facto post-flush cost (via the heal) is ~53 ms. The boot-time arm test (``moon_panel_witness_arm``) demands the
arm row's **own** completion under the same serialized, strict
discipline — capture 6 had proved the old "any ambient increment
counts" test worthless — and failure leaves the drain accept-only with
a log line, never silent. ``moon_panel_draw_wait``'s give-up line still
names the last ACCEPTED write (task + strip range + age — the probe
row is unmistakable at ``y465..466``), so culprit and victim appear in
one line; the watchdog keeps covering every path the probes do not.

The whole witness as a decision tree — where each branch was bought
with a capture (the shapes and holds above), and every dead end is
either an in-place heal or a bounded restart, never a silent wedge:

.. raw:: html

   <pre class="mermaid">
   flowchart TB
     SITE["atomic show site — panel still dark, drain called"] --> HOLDQ{"live panel hold AND no passed strip pending?"}
     HOLDQ -- "yes — preceding flush was itself dropped" --> NOOP["drain no-ops entirely (probing would phantom-accept)"]
     HOLDQ -- "no / switch frame passed, real hand to consume" --> SUBMIT["submit probe strict — single DRAM row, s_panel_mtx held across submit + witness"]
     SUBMIT --> ACC{"probe accepted by the io queue?"}
     ACC -- "rejected — WEDGED queue" --> RST["restart AT THE SITE, budgeted — RTC_NOINIT magic, 3 per boot"]
     RST --> DEGRADE["budget spent — degrade to a working clock instead of a restart storm"]
     ACC -- "accepted" --> INC{"probe's own completion incremented the counter?"}
     INC -- "yes — witnessed in the 30 ms grace, ~10 ms" --> DONE["show proceeds — the frame is provably on the panel"]
     INC -- "frozen — driver ate the flag (the NORMAL path behind a multi-strip flush)" --> HEAL["heal IN PLACE — no-op tx_param(-1) runs the driver's own drain loop, slot MINE logged, ~53 ms total"]
     HEAL --> PARK{"heal parks in get_trans_result — truly undelivered completion?"}
     PARK -- "no — queue re-armed" --> DONE
     PARK -- "yes" --> WDG["panel watchdog restarts in 3 s + 3 s — the honest fallback"]
   </pre>

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
       if (s_ready && panel_hold_live())
           return ESP_OK; /* storm: the 0x51 would ride the same bus; the wake
                           * restored the level BEFORE the hold arms, so nothing
                           * is owed -- and s_panel_pct stays truthful */
       if (s_ready && s_panel_mtx) {
           bool held = s_fade_busy;
           if (!held)
               s_fade_busy = true; /* the 0x51 rides alone (the fade's rule) */
           xSemaphoreTake(s_panel_mtx, portMAX_DELAY);
           r = bsp_display_brightness_set(pct);
           xSemaphoreGive(s_panel_mtx);
           if (!held)
               s_fade_busy = false;
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
correct. (The spin's *direct* write used to bypass the adapter's swap
and byte-swap every pixel itself on the way out — retired with the
spin subsystem.)

The P4 board's (Waveshare ESP32-P4-WIFI6-Touch-LCD-XC) MIPI-DSI
panel wants LVGL-native order (its adapter bridge
does not swap — ``lv_draw_sw_rgb565_swap`` fires only for the
"OTHER" panel interface), so the identity is correct there. Getting
this wrong is invisible in the simulator (the host renderer writes
PNGs) and shows up on the panel as every color's red and blue
channels transposed — the "the moon looks teal" bug class.

:doc:`The animation pipelines <animations>` assembles these pieces into the motion: the fade and the orbit's atomic displays (the dusk, the free spin and the boot sweep were callers until their retirement 2026-11-13).
