# Engine Fixes

A collection of fixes for Creation Engine bugs in The Elder Scrolls V: Skyrim Special Edition, Anniversary Edition, and VR.

This plugin provides comprehensive fixes for various game engine issues including memory management problems, rendering bugs, animation crashes, and other stability improvements. Engine Fixes is designed to be a foundational mod that improves game stability and performance across all Skyrim variants.

## Download

- **Skyrim SE/AE**: [Engine Fixes on Nexus Mods](https://www.nexusmods.com/skyrimspecialedition/mods/17230)
- **Skyrim VR**: [Engine Fixes VR on Nexus Mods](https://www.nexusmods.com/skyrimspecialedition/mods/17230) (VR support included in main mod)

## Requirements

### Runtime Requirements

- [SKSE64](https://skse.silverlock.org/) 2.0.17+
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

### Skyrim VR Requirements

- [SKSEVR](https://skse.silverlock.org/) 2.0.12+
- [VR Address Library for SKSEVR](https://www.nexusmods.com/skyrimspecialedition/mods/58101)

### Development Requirements

- [Visual Studio 2022](https://visualstudio.microsoft.com/) (17.10+)
- [CMake](https://cmake.org/) 3.26+
- [vcpkg](https://github.com/Microsoft/vcpkg)

## Features

Engine Fixes addresses numerous engine-level issues including:

- **Memory Management**: Fixes for memory leaks and allocation errors
- **Rendering Issues**: Shader fixes, lighting problems, and visual glitches
- **Animation Crashes**: Prevents crashes related to animation loading
- **Save System**: Improvements to save file reliability and screenshot handling
- **Performance**: Optimizations for better frame rates and stability
- **Form System**: Fixes for game object loading and caching

For a complete list of fixes, see the in-game configuration menu or the mod's documentation.

## Installation

1. Download the appropriate version for your Skyrim variant
2. Install using your preferred mod manager (MO2, Vortex, etc.)
3. Ensure SKSE and Address Library are properly installed
4. Launch the game through SKSE

## Configuration

Engine Fixes uses an INI file for configuration located at:
- `Data/SKSE/Plugins/EngineFixes.ini`

The configuration file is automatically generated with default settings on first run. Most fixes are enabled by default, but can be individually toggled if needed.

## Building

### Prerequisites

1. Set up development environment:
   ```cmd
   git clone --recursive https://github.com/aers/EngineFixesSkyrim64.git
   cd EngineFixesSkyrim64
   ```

2. For VR builds, set the environment variable:
   ```cmd
   set SkyrimVRPath=C:\Program Files (x86)\Steam\steamapps\common\SkyrimVR
   ```

### For Skyrim SE

```cmd
cmake --preset vs2022-windows-vcpkg-release-se
cmake --build --preset release-se
```

### For Skyrim AE

```cmd
cmake --preset vs2022-windows-vcpkg-release-ae
cmake --build --preset release-ae
```

### For Skyrim VR

```cmd
cmake --preset vs2022-windows-vcpkg-release-vr
cmake --build --preset release-vr
```

### Debug Builds

For debug builds with additional logging, use the debug presets:

```cmd
# SE Debug
cmake --preset vs2022-windows-vcpkg-debug-se
cmake --build --preset debug-se

# AE Debug
cmake --preset vs2022-windows-vcpkg-debug-ae
cmake --build --preset debug-ae

# VR Debug
cmake --preset vs2022-windows-vcpkg-debug-vr
cmake --build --preset debug-vr
```

## Compatibility

Engine Fixes is designed to be compatible with most other mods. However, some considerations:

- **Memory Patches**: May conflict with other memory management mods
- **Shader Fixes**: May conflict with visual overhaul mods that replace shaders
- **ENB Compatibility**: Generally compatible, but some specific fixes may need adjustment

Refer to the mod page for specific compatibility notes and load order recommendations.

## Troubleshooting

### Common Issues

**Game won't start after installation:**
- Verify SKSE and Address Library are correctly installed
- Check that you're launching through SKSE, not the vanilla launcher
- Ensure you have the correct version for your game (SE/AE/VR)

**Performance issues:**
- Try disabling individual fixes through the INI file
- Check for conflicts with other performance mods
- Verify your system meets the requirements for the fixes

**VR-specific issues:**
- Ensure VR Address Library is installed
- Verify SkyrimVRPath environment variable is set correctly
- Some VR-specific fixes may need adjustment based on your VR setup

### Logs

Engine Fixes generates detailed logs at:
- `Documents/My Games/Skyrim Special Edition/SKSE/EngineFixes.log`

Check these logs for specific error messages and debugging information.

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch
3. Test your changes thoroughly
4. Submit a pull request with detailed description

For bug reports, use the issue tracker with:
- Skyrim version and game variant (SE/AE/VR)
- SKSE version
- Engine Fixes version
- Relevant log entries
- Steps to reproduce

## License

[MIT License](LICENSE)

## Credits

- **Original Author**: [aers](https://github.com/aers)
- **VR Fork**: [rollingrock](https://github.com/rollingrock) - Created the original EngineFixesVR fork
- **VR Integration**: [alandtse](https://github.com/alandtse) - Unified VR support into upstream
- **Community Contributors**: See [Contributors](../../graphs/contributors)

Special thanks to the SKSE team and the Skyrim modding community for their foundational work that makes this project possible.