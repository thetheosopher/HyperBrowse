RAW smoke-test fixtures

Files:
- `RAW_NIKON_D1.NEF`
- `RAW_NIKON_P7000.NRW`

Source:
- `http://www.rawsamples.ch/raws/nikon/RAW_NIKON_D1.NEF`
- `http://www.rawsamples.ch/raws/nikon/RAW_NIKON_P7000.NRW`

Purpose:
- exercise the shared LibRaw-backed thumbnail decode path for both NEF and NRW
- exercise the shared LibRaw-backed full-image decode path in `tests/smoke.cpp`

These files are used only as test fixtures for local smoke coverage.