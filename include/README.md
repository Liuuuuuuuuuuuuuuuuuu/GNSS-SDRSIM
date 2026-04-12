# Header Layout

Functional headers are grouped under `include/` by domain.

- `include/core`: base shared modules and low-level channel/BCH/global types
- `include/nav`: navigation, ephemeris, timing, and nav message generation
- `include/geo`: coordinate/path geometry interfaces
- `include/runtime`: runtime bridge/integration headers (GNSS RX, RID RX, Wi-Fi RID, USRP)

Build system auto-discovers all include subdirectories via `Makefile`.
