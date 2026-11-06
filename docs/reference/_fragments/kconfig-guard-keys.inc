.. NOTE: shared fragment — see kconfig-board-choice.inc for the conventions
   (no headings, ../<section>/<page> :doc: targets).

Three integer keys, all defaulting to ``0``, exist in the board
Kconfig only to keep the component manager's version solver from
aborting during configure:

.. list-table::
   :header-rows: 1
   :widths: 44 10 46

   * - Symbol
     - Default
     - Type (required)
   * - ``ESP_HOSTED_HOST_USB_CDC_ACM_MANIFEST``
     - ``0``
     - ``int``
   * - ``ESP_HOSTED_HOST_USB_CHERRYUSB_MANIFEST``
     - ``0``
     - ``int``
   * - ``ESP_HOSTED_HOST_USB_RNDIS_MANIFEST``
     - ``0``
     - ``int``

The reason for their existence: the ``esp_hosted`` 3.x manifests on
the registry declare optional USB-*host* transport dependencies
(``espressif/usb_host_cdc_acm``, ``cherry-embedded/cherryusb``,
``espressif/iot_usbh_rndis``), each gated by an ``if:`` clause on one
of these symbols — Kconfig symbols that only ``esp_hosted`` 3.x's own
Kconfig defines. The version solver evaluates the ``if:`` clauses of
*every* candidate version while resolving (``esp_wifi_remote`` 0.14.5
depends on ``esp_hosted >=0.0.6``, an unbounded range), so those
clauses are parsed against this project's ``sdkconfig.json`` even
though the tree pins ``esp_hosted`` 1.4.7 (which does not define the
symbols). Once ``esp_hosted`` 3.1.0 (2026-11-05) landed on the
registry, a re-resolution hit the interface-v4 "missing required
kconfig option" fatal (``prepare.py`` exit 10). Defining the three
keys here makes the clauses evaluate to *false* (the C6 is
transported over SDIO — there is no USB-host backend) instead of
aborting the configure.

**They must be ``int``, not ``bool``.** The ``if:`` clauses compare
against ``1`` (``$CONFIG{...} == 1``), and the if-clause parser
(``if_parser.py``) dispatches on the ``sdkconfig.json`` *value type*:
a ``bool`` value makes it evaluate ``eval_bool("1")`` — a hard
"Invalid boolean" error — while an ``int`` value is a plain ``0 == 1``
comparison. That is why the three keys are ``int`` with a default of
``0``: a ``bool`` key would serialize to a boolean value in
``sdkconfig.json`` and trip the parser the same way, even if it were
off by default.
