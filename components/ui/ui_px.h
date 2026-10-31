#ifndef EEZ_UI_PX_H
#define EEZ_UI_PX_H

/* Panel-relative UI scaling.
 *
 * The UI was designed at the S3's 466x466 and scales to the panel by
 * its height (both panels are square). UI_PX() is exact at 466, so the
 * S3 layout is bit-for-bit unchanged; at the P4's 720 it is the
 * 720/466 upscaling of every layout literal. Full-panel extents use
 * UI_PANEL_PX directly.
 *
 * The host simulator compiles the device screens unmodified with
 * -DPANEL_SIZE=N (uisim build.sh), so the panel size comes from there;
 * the device gets it from the BSP (BSP_LCD_H_RES).
 */
#if defined(PANEL_SIZE)
#define UI_PANEL_PX PANEL_SIZE
#else
#include "bsp/display.h"
#define UI_PANEL_PX BSP_LCD_H_RES
#endif

#define UI_PX(x) (((x) * (UI_PANEL_PX)) / 466)

#endif /* EEZ_UI_PX_H */
