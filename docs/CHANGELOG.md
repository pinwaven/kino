# Changelog

All notable changes to the **Kino Biomarker Analyzer** project will be documented in this file.

## [1.0.0] - 2026-04-26

### Added
- **Modular Project Structure**:
  - `src/hal/`: Hardware Abstraction Layer for Display and Touch.
  - `src/ui/`: UI management and layout.
  - `src/ui/assets/`: Dedicated storage for SVG and image descriptors.
  - `src/core/`: Placeholder for biomarker analysis algorithms.
- **Documentation**: Initialized `docs/` folder and this `CHANGELOG.md`.

### Changed
- **Entry Point Refactoring**: Simplified `kino.ino` to act as a high-level orchestrator.
- **SVG Handling**: Migrated raw SVG strings to constant arrays in `src/ui/assets/` to ensure compiler compatibility.
- **Build System**: Verified compilation using `arduino-cli` with local library inclusion.

### Fixed
- **LVGL Library Corruption**: Performed a major cleanup of the `libraries/lvgl` folder.
  - Identified and quarantined over 50 legacy LVGL v8 files/folders that were conflicting with v9 components.
  - Resolved `multiple definition` and `fatal error: missing header` issues caused by the v8/v9 mix.
- **Compiler Error**: Fixed "initializer element is not constant" in SVG descriptor by changing pointer declarations to array declarations.

### Removed
- Redundant `waven_logo.c` bitmap file (migrated to SVG).
- Duplicate `pin_config.h` from `libraries/Mylibrary/`.
- Outdated LVGL v8 components moved to `libraries/lvgl/v8_backup/`.
