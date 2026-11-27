Ship no alarms by default
   A fresh flash must not arm an unasked-for ring: the project default became comments-only (``# no alarms``) in :commit:`dadfc2c5`.
   See :doc:`/reference/spiffs-store`.

S3's alarm interrupt gated on the rule cache
   The S3's ALM0 interrupt fired on the RTC's latched AF flag.
   A latched AF with no rule scheduled is a stale latch (for example, after the last rule was deleted), so :commit:`08645912` gated the fire path on the scheduler's cache (``clock_alarm_get_next_scheduled``), matching the P4 parity.
   See :doc:`/implementation/clock-sleep`.

0-rule alarm reload fires the update callback
   Deactivating the last rule reloaded the scheduler with zero rules but did not fire the same alarm-update callback a rule write does, so the home's alarm row kept showing the deactivated time from a stale cache (on the PCF85063A boards the cache clear was even compiled out).
   :commit:`94806f03` dropped the cached next-alarm time on **every** board and fired the callback on the 0-rule reload too, so the row goes to ``--:--`` immediately.
   See :doc:`/ui/settings-alarm`.
