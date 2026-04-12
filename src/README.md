# Source Layout

This project now groups root simulation/runtime sources by domain under `src/`.

- `src/core`: low-level shared core modules (`bch`, `channel`, `globals`)
- `src/nav`: navigation/message/ephemeris modules (`navbits`, `rinex`, `orbits`, `iono`)
- `src/geo`: coordinate and path math (`coord`, `path`)
- `src/runtime`: runtime integrations (`gnss_rx`, `rid_rx`, `wifi_rid_rx`, `usrp_wrapper`)

Entrypoints remain at repository root:
- `main.c`
- `main_gui.cpp`

Build mapping is maintained in `Makefile` via `COMMON_SRCS`.
