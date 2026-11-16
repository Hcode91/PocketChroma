# PocketChroma

An Android emulator for the 2014 M&D Monon Color handheld (AX208 / enhanced-8051 CPU, 320×240 LCD, SPI-flash cartridges).

The emulation core is a standalone port of MAME's driver and AXC51 CPU core, with a thin compatibility shim instead of the MAME framework.

## What you need
- **Boot ROM:** the AX208 chip's 8 KB internal ROM (`ax208.bin`, SHA-1 `0dc7ab9bdaf73231d4d6627fe6308fe8103e1bbc`). It isn't included; dump it from your own hardware.
- **Carts:** raw SPI flash dumps (`.bin`, usually 16 MB) or a `.zip` containing one.

## Features
- Touch controller (D-pad, A, B, L, R, Menu, Pause) with haptics; auto-hides when a gamepad is used
- Gamepad and keyboard support (WASD, J/K, Q/E, Tab, Space)
- Battery saves (flash 0x50000–0x5FFFF) written to `saves/<game>.sav`
- Save states, fast-forward, reset

## Known limitations (inherited from MAME)
- **No music.** Each cartridge has its own music chip whose program hasn't been dumped. Sound effects (AX208 DAC) do play.
- Card/badge readers and infrared link aren't emulated.
- Timing and some graphics are approximate.

## Building
GitHub Actions builds debug and release APKs on every push (see the **Actions** tab → artifact `PocketChroma-apk`).

Desktop smoke test for the core:
```
g++ -std=c++17 -O2 -o headless tools/headless.cpp core/*.cpp
./headless ax208.bin game.bin 600 out.ppm
```
