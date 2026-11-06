The user interface
==================

One page per screen. The device has sixteen screens — the five moon
screens (the clock plus the moon, in the product's sleep/wake states)
and the eleven settings screens (the hub and its ten sub-screens) —
enumerated by ``enum ScreensEnum`` in
:doc:`../code/index` (``components/ui/screens.h``). Each page carries
simulator screenshots for BOTH panels (S3 466×466 first, P4 720×720
second), annotated callouts naming every pointing element, and a
per-element table linking the element to its code and its Kconfig /
SPIFFS option.

The screens share one clock widget (the layout contract in
``screens.h``): the digits you see on every moon screen are the same
three objects, reparented on the screen switch.

.. toctree::
   :maxdepth: 2

   modem-sleep
   wifi
   light-sleep
   deep-sleep
   alarm
   settings-hub
   settings-alarm
   settings-pomodoro
   settings-alarm-time
   settings-light-sleep
   settings-deep-sleep
   settings-wifi
   settings-wifi-password
   settings-time
   settings-tz
   settings-alarm-sound

(The per-page content — screenshots, annotated callouts, element
tables — lands in stage 6.)
