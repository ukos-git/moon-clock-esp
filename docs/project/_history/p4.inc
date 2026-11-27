P4 on-device acceptance (M3 and M4)
   The P4 port's acceptance ran on the real board in two milestones, before the motion pipelines were retired.
   M3 (the moon at 618 px): the boot sweep ran **336/336** frames with 0 WDT/panic; the sweep's final full-resolution bake was **599 ms @ 618 px / 959 ms @ 650 px**; the dusk ran ~43 s; the direct-write path settled at **103 rows** (6 strips, 127 KB) per burst (the DPI-FIFO underrun at 206 rows: X16 + XIP), and the panel's async-DMA throughput floor showed **~3,900 rejects, 0 dropped strips** over a long session.
   M4 (the disk upscaled to 650 px, the UI upscaled): re-verified with the quadrupled sweep — **1344/1344** frames, 0 WDT/panic, ~40.5 min wall, bake median **1543 ms @ 650 px** (the 2048-albedo era's 1138–2083 ms spread); the direct-write burst was **65 rows** (10 strips, 84.5 KB).
   The binary was **1,575,152 B** at the TZ-land time (the full chronology is in :doc:`the binary-size page <../verification/binary>`).
   The C6 Wi-Fi link was verified over SDIO (association, SNTP and scan) at M2, before the moon port (M3) and the UI upscale (M4) started.
   These numbers were recorded with the migration of the P4 chapters to this site, :commit:`47c4e41d`; the motion pipelines they measured are gone, and the strip pitch survives as the pacing constant of the atomic displays' invalidations.
   See :doc:`/implementation/p4-port`.
