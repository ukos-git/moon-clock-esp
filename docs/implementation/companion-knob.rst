The companion knob input
========================

The **Waveshare ESP32-S3-Knob-Touch-LCD-1.8** (see
:doc:`the device page <../devices/knob>`) runs its own ESP-IDF project
(``knob/``, a sibling build like ``build/p4``: own build dir, own
dependency lock, own gitignored ``sdkconfig``). The firmware that ships
today is the **companion-input device** of the plan — the knob's dial
and touch zones drive the *clock's* screens over the LAN. (The board's
intended product role is a fully-working **moon display on its own**;
that milestone builds on the bring-up recorded on the device page —
this page documents the input firmware that is on the bench.)

Architecture
------------

.. code-block:: text

    knob S3R8 (knob/ project)                      clock (this project)
      ST77916 QSPI 360x360 (LVGL 9.6 mirror)        main: ui_init + companion_start()
      CST816S touch zones                           components/companion:
      EC1 dial A=GPIO8 B=GPIO7                        mDNS _moonclock._udp.local
        |  knob_link: UDP unicast, fixed              UDP listener task (core 0)
        |  10-byte frames + seq dedup                   -> FreeRTOS queue
        +-------------------------------------------->   drained by ui_tick_timer_cb
                                                          -> screens_companion_handle()
                                                             (LVGL task ONLY — every
                                                             effect is a same-context
                                                             UI call like a touch event)

The clock is **authoritative**: every frame kind is gated against the
*current* screen and dropped silently otherwise. The knob keeps no
state of its own — its ring and field list are mirrors, and its status
line names the clock so the user puts the clock on the right screen
before dialing.

Wire contract
-------------

``components/companion/include/companion_proto.h`` is the single source
(both projects compile it verbatim; ``host_sim/companion_proto_test.c``
compiles it on the host). One frame is exactly ``COMPANION_FRAME_LEN``
(10) bytes, packed ``<HBBHBBbB``::

    magic u16 'MC' | version u8 | type u8 | seq u16 | token u8 | field u8 | delta i8 | rsvd u8

* ``seq`` — an exact repeat of the last accepted sequence is dropped
  (UDP retransmit / duplicate defence).
* ``token`` — must equal ``COMPANION_TOKEN`` unless that is 0 (accept
  any, the default). LAN posture, see Security below.
* ``COMPANION_DWELL_MS`` (3000, bench-tuned 2026-11-08) — also
  single-sourced here: the knob commits its hold-to-commit dwell on it
  and the clock mirrors the same value as the wheel's countdown ring.
  A drift would make the ring lie.

Frame kinds and their screen gates (``screens_companion_handle``,
``components/ui/screens.c``):

.. list-table::
   :header-rows: 1
   :widths: 22 30 48

   * - Frame
     - Valid on
     - Effect
   * - ``HELLO``
     - any screen
     - probe; logged by the listener (``HELLO from <ip> (seq n)``).
   * - ``KEY_HOT`` (delta = slot)
     - WiFi password
     - highlights the wheel slot (DTMF-style centre preview) and arms
       the dwell countdown ring.
   * - ``KEY_ADD`` (delta = slot)
     - WiFi password
     - commits the slot into the entry (the dwell's payload; the
       touch RELEASED path shares the same body).
   * - ``KEY_BACK`` / ``KEY_COMMIT`` / ``ABORT``
     - WiFi password
     - backspace / save + sync (the check key's body) / discard —
       with an empty entry ``ABORT`` exits (the circle-xmark key's
       body).
   * - ``KEY_MODE`` (delta 0..3) / ``FOCUS_NEXT``
     - WiFi password
     - register switch (abc / ABC / 123 / #!), shared with the
       register arc.
   * - ``STEP`` (field + delta)
     - time / alarm time / pomodoro / TZ
     - clamped ``lv_roller_set_selected`` on the named field
       (``HOUR…YEAR``, ``ALM H/M``, ``ALM DAYS``, ``POM H/M``, ``TZ``).
   * - ``COMMIT`` / ``ABORT``
     - the roller screens
     - the screen's existing release-commit body / exit (the TIME
       screen's X exit commits — the knob mirrors that).

Accepted non-``HELLO`` frames reset the idle timer — a mid-entry sleep
drop is what the touch path resets against.

The dial decoder (a bench discovery)
-------------------------------------

The EC1 encoder behind the dial is **not a quadrature wheel**. Each
detent pulses exactly ONE of its channels (A = GPIO 8, B = GPIO 7, 10K
pull-ups) and *which* channel pulsed is the direction — a UP/DOWN
pulse pair. The shipping decoder mirrors the vendor's own driver
(``bidi_switch_knob.c`` from the ESP-IDF demo) 1:1: a 3 ms poll task,
two independent debounced channel counters (edge accepted after two
consecutive identical samples), debounced **A rise = +1**, debounced
**B rise = −1**. Every quadrature-shaped decoder that was tried before
this discarded exactly half the detents — an A-edge-count scheme lost
all B-detents (the "only moves one way" symptom), and an A-rise +
B-sign scheme found no sign (B never shares the A pulse's moment: the
channels pulse on *alternate* detents). The counter is read through a
portMUX-guarded getter; the UI polls it on its own timer and batches
the detents since the last tick into one frame.

Keyboard: dwell to commit
--------------------------

The dial does not push, so nothing commits on a detent crossing: the
knob sends ``KEY_HOT`` per detent and only sends ``KEY_ADD`` after the
dial has **settled on the slot for 3 s** (one shot per settle; a new
detent restarts it). While a slot is hot, the clock drains a thin ring
around the wheel's centre preview (r 66 — the empty gap between the
centre box and the register track) over exactly that window and the
hint line reads "hold to add". The ring is a purely **additive**
companion affordance: only the companion handler arms it, the touch
path never sees it, and any hot change (a touch grab included) drops
it. On the clock's own keys, the check accepts and the circle-xmark
discards (see :doc:`the keyboard design <../keyboard>`).

Rollers map for free: ``STEP`` drives the existing single-row
``lv_roller`` widgets; the screens' RELEASED-time commit functions were
made callable and are shared verbatim — the knob sends an explicit
``COMMIT`` instead of a release.

Power model
-----------

The clock is airplane-first (the radio is up only for syncs), so the
listener would be unreachable between them —
``COMPANION_KEEP_WIFI`` keeps the association alive through the
airplane/light-sleep hand-offs on the wall-powered board (P4: ``y``;
the battery S3: ``n``). Deep sleep always deinitialises the radio —
the companion is useless while the clock is hidden. The knob itself is
battery-powered: its deep-sleep + rotate-wake power milestone (K5) is
separate from this path.

Security posture (stated)
--------------------------

LAN-trusted: UDP, no encryption, and the shared token byte keeps a
curious neighbour off the dial — it is **not a secret** (it sits in
two ``sdkconfig.defaults`` files). Anyone on the LAN who can reach
``COMPANION_PORT`` (50188) can drive exactly what the screen gate
allows on the currently-open screen — no frame reaches a settings
screen the user has not opened, and the password field itself is
reachable only while the keyboard screen is on top.

Verification
------------

* ``host_sim/build/companion_proto_test`` — the transport half
  (accept / bad magic / bad version / bad type / short / long /
  duplicate seq / seq wrap / token).
* ``host_sim/uisim/build/uisim --companion`` — the device-side gate
  driven through ``screens_companion_handle``: wrong-screen drops,
  wheel entry via HOT/ADD, backspace, discard/exit, roller clamping,
  and the dwell ring (armed by HOT, dropped by ADD, self-hiding on
  timeout).

Build and flash (the knob project)

.. code-block:: bash

    source ~/esp/esp-idf/export.sh
    idf.py -C knob -B ../build/knob build          # from the repo root
    idf.py -C knob -B ../build/knob app-flash -p <port>   # spiffs-preserving

Identify the port first (``esptool.py --port <port> chip_id`` — the
knob is the ESP32-S3 on the bench; see the device page's bench notes).
``knob/spiffs/knob_credentials.txt`` is gitignored like the clock's.
