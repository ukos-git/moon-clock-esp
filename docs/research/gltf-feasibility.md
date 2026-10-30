# Research: LVGL glTF on the ESP32-S3 / ESP32-P4

Date: 2026-10-30 · Method: live fetch of the LVGL 9.6 docs (headless browser),
inspection of the vendored LVGL 9.6.0 in this repo, local ESP-IDF 5.5.1 driver
inventory, Espressif P4 product page

## Verdict

**No, on both chips.** LVGL's glTF extension requires an OpenGL ES context, and
neither the ESP32-S3 nor the ESP32-P4 has a 3D GPU (no OpenGLES). LVGL 9.6
ships no software/CPU 3D path as a fallback. The project's existing per-pixel
displaced-sphere moon pipeline (see [moon-rendering.md](moon-rendering.md)) is
not something glTF would replace or improve here.

## Evidence

1. **LVGL 9.6 glTF is GPU-only — no CPU path.** The docs page
   (https://lvgl.io/docs/open/libs/gltf, fetched 2026-10-30) states the
   extension "relies on OpenGL ES 2.0" and "uses OpenGL ES 3.0 shaders (GLSL
   version 300 es)", and requires enabling `LV_USE_OPENGLES` plus a draw unit —
   `LV_USE_DRAW_OPENGLES` or the NanoVG-GL backend — plus the external
   dependencies `fastgltf` (a C++20 parser) and `libwebp`. In the vendored LVGL
   9.6.0 (`managed_components/lvgl__lvgl/Kconfig`), the gating is:

   ```
   config LV_USE_GLTF
       bool "glTF"
       select LV_USE_3DTEXTURE
       depends on LV_DRAW_HAS_3D_SUPPORT
   ```

   and `LV_DRAW_HAS_3D_SUPPORT` is selected by exactly two configs:
   `LV_USE_DRAW_NANOVG` ("Draw with the NanoVG vector library on top of OpenGL
   / OpenGL ES shaders") and `LV_USE_DRAW_OPENGLES` ("keeping rendered widgets
   and images as GPU textures"). Both need a GL context. The glTF sources under
   `managed_components/lvgl__lvgl/src/libs/gltf/` wrap every file in
   `#if LV_USE_GLTF` (compiling them out here) and hard-reference GL calls
   (`lv_gltf_view_render.cpp`, `glGetUniformLocation` in
   `gltf_data/lv_gltf_data_internal.hpp`) — there is no rasterizer branch that
   would run without a GPU. This project has neither backend enabled in
   `sdkconfig` (`LV_CONF_MINIMAL`, no `LV_USE_DRAW_OPENGLES`, no
   `LV_USE_DRAW_NANOVG`).

2. **Neither chip can provide the context.**

   - **S3:** no 3D GPU at all (doc §13: "No GPU / no hardware 2-D"; the only
     accelerators are PPA/2D-DMA class 2D units, and PPA is off in this
     project).
   - **P4:** Espressif's product page (2026-10-30) lists the hardware
     accelerators as "an integrated hardware Pixel Processing Accelerator (PPA)
     and 2D-DMA" plus MIPI-DSI/CSI, an H.264 encoder and an ISP — a 2D
     accelerator set, no 3D GPU, no OpenGLES. The local ESP-IDF 5.5.1 install
     (authoritative for this project's toolchain) contains **zero**
     OpenGLES/GPU drivers for any target, and LVGL's own ESP-IDF integration
     docs (https://lvgl.io/docs/open/integration/chip_vendors/espressif) cover
     only the 2D accelerators (DMA2D, PPA). So there is no context to hand to
     the glTF renderer, and no Espressif-supported way to make one. The P4 port
     of this project is additionally still open work (rev 1.3 silicon; a full
     port, not a config toggle), so even the board-side question is moot until
     that lands.

3. **Even if it could run, the S3 has no flash headroom.** The `factory`
   partition is 1,425,408 B and the app sits at ~99.7 % of it (doc §3.1; every
   feature costs bytes). A C++20 glTF parser plus libwebp plus an OpenGLES
   driver plus GLSL shaders is orders of magnitude beyond the few KiB of free
   space. (The P4 would have more flash, but the missing OpenGLES context is
   the fundamental blocker there regardless.)

4. **The project already does what glTF would offer, better.** The moon is a
   per-pixel displaced-sphere renderer with real 2048×1024 DEM normals and a
   NASA-validated image gate; the original research
   ([moon-rendering.md](moon-rendering.md)) explicitly rejected GPU libraries
   at 400–466 px ("GPU libs … add dependency weight with little benefit").
   There is no other 3D content in the app: everything else is LVGL widgets,
   embedded FontAwesome glyphs, and the hand-rolled concentric reveal. glTF's
   animation/skining/IBL/camera feature set has no buyer here.

## What would change the verdict

- LVGL ships a **software/CPU 3D backend** (a rasterizer path for `LV_USE_GLTF`
  that does not require `LV_DRAW_HAS_3D_SUPPORT` from an OpenGLES context).
  Check `LV_USE_GLTF`'s Kconfig dependencies at the next LVGL major bump.
- The P4 ecosystem gains a **real 3D GPU with an OpenGLES driver** in ESP-IDF
  (a different P4 revision with a 3D block, or a GPU coprocessor the BSP
  exposes as a GL context). Neither the current P4 feature list nor the IDF
  5.5.1 driver tree shows this.
- The product grows **3D content that isn't the moon** (a mesh model,
  animation, IBL-lit scene) AND the chip question above resolves — at which
  point the flash budget also needs a real answer, not an estimate.
