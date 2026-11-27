Design history
==============

.. brief:
   Reader: a developer who wonders why something works the way it does,
   or who meets code for a feature that no longer runs.
   They already know: the current design, from the in-depth pages.
   After this page they can: find when and why a design changed, and
   the commit that changed it.
   Page type: reference (a changelog grouped by area).

The other pages describe the clock as it is now. This page keeps the
story of how it got there: features that were added and removed,
designs that were replaced, and the decisions behind them. Each entry
links the commit that made the change; the commit page on GitHub
carries the date.

Entries are grouped by the part of the project they belong to, newest
first within each group where the order is known. The last section
keeps the full mechanism of the pipelines that were removed, so the
rest of the site can state the current design without narrating the
history.

The moon on the home screen
---------------------------

.. include:: _history/moon-home.inc

Clock and sleep
---------------

.. include:: _history/clock.inc

The round keyboard
------------------

.. include:: _history/keyboard.inc

The user interface
------------------

.. include:: _history/ui.inc

The P4 port
-----------

.. include:: _history/p4.inc

The housing
-----------

.. include:: _history/housing.inc

Appendix: the removed mechanisms, in full
-----------------------------------------

.. include:: _history/animations.inc
