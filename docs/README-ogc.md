# Nintendo GameCube/Nintendo Wii

SDL port for the Nintendo GameCube and Nintendo Wii [Homebrew toolchain](https://devkitpro.org/).

Credits to:

-   The awesome people who ported SDL to other homebrew platforms.
-   The Devkitpro team for making all the tools necessary to achieve this.

## Building

To build for the Nintendo GameCube or Wii, make sure you have devkitPPC and cmake installed and run:

```bash
cmake -S. -Bbuild -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Wii.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build
```

## Notes

-   Currently only software rendering is supported.
