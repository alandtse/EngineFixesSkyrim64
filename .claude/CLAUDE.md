# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

EngineFixes is a C++ SKSE plugin for The Elder Scrolls V: Skyrim that provides various bug fixes and performance improvements for the game engine. It supports Skyrim SE (1.5.97), Skyrim AE (1.6.1170), and Skyrim VR (1.4.15) through conditional compilation.

## Build System

This project uses CMake with vcpkg for dependency management and supports Visual Studio 2022.

### Build Commands

```bash
# Configure for Skyrim SE (Release)
cmake --preset vs2022-windows-vcpkg-release-se

# Configure for Skyrim SE (Debug)
cmake --preset vs2022-windows-vcpkg-debug-se

# Configure for Skyrim AE (Release)
cmake --preset vs2022-windows-vcpkg-release-ae

# Configure for Skyrim AE (Debug)
cmake --preset vs2022-windows-vcpkg-debug-ae

# Configure for Skyrim VR (Release)
cmake --preset vs2022-windows-vcpkg-release-vr

# Configure for Skyrim VR (Debug)
cmake --preset vs2022-windows-vcpkg-debug-vr

# Build
cmake --build --preset release-se    # or debug-se, release-ae, debug-ae, release-vr, debug-vr

# Regenerate CMake file lists after adding/removing source files
python ProjectGen.py
```

### Required Environment Variables

- `VCPKG_ROOT`: Path to vcpkg installation
- `Skyrim64Path`: Path to Skyrim SE/AE installation (for automatic DLL copying)
- `SkyrimVRPath`: Path to Skyrim VR installation (for VR builds with automatic DLL copying)

### Build Configuration

- Target: x64 Windows DLL
- C++ Standard: C++23
- Dependencies managed through vcpkg (see vcpkg.json)
- Precompiled headers via src/PCH.h
- Automatic copying to Skyrim directory when `COPY_BUILD=TRUE`

## Architecture

### Module Structure

- **src/main.cpp**: SKSE plugin entry points and lifecycle management
- **src/fixes/**: Individual game engine bug fixes
- **src/patches/**: Game behavior modifications and optimizations
- **src/memory/**: Custom memory allocators and management systems
- **src/warnings/**: Runtime warning systems
- **src/settings.h**: Configuration management using INI/TOML

### Key Components

1. **SKSE Integration**: Uses CommonLibSSE for game API access
2. **Hook System**: SafetyHook library for runtime code patching
3. **Memory Management**: Custom allocators including Havok integration
4. **Conditional Compilation**: `BUILD_SKYRIMAE` flag for version-specific code
5. **Preload Architecture**: Plugin must preload via d3dx9_42.dll for full functionality

### Development Workflow

1. Add new source/header files to appropriate directories
2. Run `python ProjectGen.py` to update CMake file lists
3. Configure and build using CMake presets
4. Test with target Skyrim version

### File Organization

- Headers: Organized by functionality (fixes/, patches/, memory/, warnings/)
- Implementation: Matching .cpp files in same directory structure
- CMake: Build configuration in cmake/ directory
- Dependencies: External libraries in extern/ and managed via vcpkg

### Important Notes

- Plugin requires preloading for fixes to work properly
- Uses extensive runtime patching of game engine code
- Memory fixes replace default allocators for performance
- Configuration loaded from INI files at runtime