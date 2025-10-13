0.0.1

## Unreleased

### Packaging

- Added MANIFEST and vendored public ASTL headers (including generated
  `astl_version.h`) under `python/astl/include/astl` so wheels built from an
  sdist compile without needing the full repository layout.
- `python/setup.py` now falls back to the vendored include directory if the
  top-level `include/` tree is absent during build (isolated sdist / CI
  environments). This resolves previous
  `fatal error: astl/astl_errors.h: No such file or directory` and
  `astl_version.h` missing failures.
