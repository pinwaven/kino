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

### Added
- **GUI Capability Test**: 
  - Verified SVG rendering support.
  - Verified LVGL native animation system compatibility with SVG assets.
  - Added `src/ui/assets/animated_dot_svg` as a test asset.
- **WiFi HAL Implementation**:
  - Added `src/hal/WiFi.h/.cpp` for asynchronous connection management.
  - Successfully verified connection to "WAVEN-SHW" with IP display.
  - Added git-ignored `src/hal/local_config.h` for credentials.
- **UI Stabilization**:
  - Simplified UI to a minimal "KINO" label and status display to resolve intermittent screen blackouts.
  - Enabled Montserrat 24 and 48 fonts in `lv_conf.h`.
- **Build System Update**:
  - Updated FQBN to support 16MB Flash and OPI PSRAM (`PartitionScheme=app3M_fat9M_16MB`).
  - Resolved "Sketch too big" error by expanding the APP partition.

### Removed
- Root `assets/` folder (consolidated into `src/ui/assets/`).
- Redundant `waven_logo.c` bitmap file (migrated to SVG).
- Duplicate `pin_config.h` from `libraries/Mylibrary/`.
- Outdated LVGL v8 components moved to `libraries/lvgl/v8_backup/`.
