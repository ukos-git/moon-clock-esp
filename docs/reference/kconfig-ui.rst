UI component Kconfig
====================

``components/ui/Kconfig`` ("UI Component Configuration") is the
project's largest menu: the moon-pipeline behaviour, the sleep
tunables, the IMU/orbit coupling, the demo mode, and the headless test
hooks. Every value below is the current source default.

.. include:: _fragments/kconfig-ui.inc

Conventions
-----------

Remember the project's Kconfig rules (:doc:`the build system
<build-system>`): a ``n`` leaves the macro *undefined* (the code gates
on ``#if CONFIG_…``), all runtime values go through
``#ifndef``-seeded getters, and the values the settings screens can
change at runtime (idle timeout, sleep countdown, the animation flags,
the timezone, the alarm sound) are seeded by these compile-time
defaults but owned by the settings store at runtime
(:doc:`the SPIFFS store <spiffs-store>`).
