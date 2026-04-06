# Portable Package Layout

This folder documents the file groups used by the portable build.

The staging script creates a self-contained layout under a versioned directory such as `dist/GNSS-SDRSIM-1.0-portable/`:

- `bds-sim`: launcher entry point
- `bin/`: GPU executables
- `lib/`: bundled shared libraries
- `BRDM/`: ephemeris data
- `fonts/`: GUI fonts
- `ne_50m_land/`: map background data
- `scripts/`: helper scripts for rebuild and GPU checks

Build it with:

```bash
bash scripts/build-portable.sh
```

Or from Makefile:

```bash
make portable
```