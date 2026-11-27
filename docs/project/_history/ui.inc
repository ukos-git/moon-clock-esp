Wall-mount trim became a roller
   The ZERO ANGLE trim used to be a thin arc handle at the panel rim, too small to grab reliably on a touch panel.
   :commit:`81bce40c` replaced it with the house 0..359 single-slot roller (the alarm/time/pomodoro picker); the roller is the value display, and a release commits and re-pushes the screen tilt.
   See :doc:`/ui/settings-display`.

Companion input removed
   The knob's earlier role was a companion input: UDP frames driving the clock's screens.
   At the owner's direction the companion code was removed from the whole repo and the clock lost its listener in :commit:`955cf91c`; the knob kept only the standalone moon display.
   See :doc:`/devices/knob`.

Demo mode and the hub ring radius
   :commit:`1d994b0d` added demo mode (an MM:SS countdown that walks the screens in order) and set the settings hub's ring radius to the balanced value that kept the exit disk and the icons an equal distance apart on the 466 px panel.
   See :doc:`/ui/settings-hub` and :doc:`/verification/demo-mode`.

Clock digit roll adopted the settings motion
   The split-flap minute-pair roll (``UI_CLOCK_ROLL_ENABLE``, off by default) took its ease-out two-line roll from the settings roller in :commit:`5f50c8d0`.
   It fires exactly when the seconds arc completes its cycle and read as a spurious arc animation, so the default stayed off.
   See :doc:`/implementation/animations` (the clock digit roll).

Orientation re-sync gated on a stable on-axis pose
   :commit:`fa0f419e` fixed a rotated ghost in the reveal and gated the 2 s press orientation re-sync so a palm released mid-flip is dropped rather than adopted as the new home pose.
   See :doc:`/implementation/clock-sleep` (the orientation pipeline).
