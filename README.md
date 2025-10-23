markdown
SD Cloner — (for use with PicoCalc Edition, Clockwork Tech, LLC (R))
C Engine + GTK3 GUI for Safe SD Card Imaging and Cloning

![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)
![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)
![GTK 3](https://img.shields.io/badge/GTK-3.0-green.svg)
![Language: C17](https://img.shields.io/badge/language-C17-orange.svg)
![OS: Pop!_OS 22.04 Certified](https://img.shields.io/badge/OS-Pop!__OS%2022.04%20Certified-blueviolet.svg)
![Architecture: Engine+GUI](https://img.shields.io/badge/Architecture-Engine+GUI%20Modular-yellow.svg)
![Target: PicoCalc](https://img.shields.io/badge/Target-PicoCalc-orange.svg)

Overview

SD Cloner (PicoCalc Edition) is a safe, modular SD card cloning tool built for Linux systems such as Pop!_OS 22.04 LTS.  
It combines a hardened C-based engine for block-level imaging and a GTK3 graphical interface that provides a user-friendly workflow with visual progress and device validation.

Originally developed by Dr. Eric Oliver Flores Toro, this tool for cloning an SD Card, enabling users to backup, duplicate, or restore bootable SD cards reliably — including those used in the PicoCalc handheld system.

Goals and Design Principles

| Objective | Description |
||-|
| Reliability | Every operation is conservative and validated; the source is always read-only. |
| Safety | Explicit device verification prevents accidental overwrite of system drives. |
| Flexibility | Supports raw imaging, FS-aware shrinking, and burning from `.img` / `.img.gz` files. |
| Usability | Intuitive GTK interface, menus with accelerators, and non-blocking progress feedback. |



Architecture

Engine (Core)
Files: `sdcloner_engine.c`, `sdcloner_engine.h`  
Implements:
- Bit-for-bit imaging and filesystem-aware cloning.
- Automatic space estimation and compression.
- Logic for shrinking a 128 GB image to a smaller 32 GB target (if space allows).
- Safe `dd`, `gzip`, `rsync`, and `parted` orchestration.

GUI Frontend
File: `sdcloner_gui.c`  
Built with GTK 3, featuring:
- Device selection for true block devices (`/dev/sdX`).
- Non-blocking worker threads with a pulsing progress bar.
- Menus:
  - File → Open Image (.img/.img.gz)
  - Tools → Read Source / Burn Destination
  - Help → About / Technologies

Directory Layout
```

/src
├── main.c
├── sdcloner_engine.c
├── sdcloner_engine.h
├── sdcloner_gui.c
/docs
├── whitepaper.pdf
├── sdcloner_tree.txt
/images
├── screenshot_main.png
└── screenshot_about.png
Build Instructions

 Prerequisites
```bash
sudo apt update
sudo apt install -y build-essential libgtk-3-dev linux-libc-dev \
                    parted dosfstools e2fsprogs util-linux rsync gzip \
                    exfatprogs


 Compilation

```bash
gcc -O2 -Wall -Wextra -c sdcloner_engine.c -o sdcloner_engine.o
gcc -O2 -Wall -Wextra sdcloner_gui.c sdcloner_engine.o -o sdcloner_gui \
    `pkg-config --cflags --libs gtk+-3.0` -pthread
```
Quick Start

 Clone or Image a Source Card

```bash
./sdcloner_gui
```

1. Go to Tools → Select Source…
2. (Optional) Choose Destination…
3. Click Read Source to create an image in `~/SDCloner/images/clone-.img.gz`
4. Or click Burn to Destination to duplicate directly.

 Burn an Existing Image

1. File → Open Image… → select `.img` or `.img.gz`
2. Choose destination `/dev/sdX`
3. Tools → Burn to Destination

FS-Aware Clone Logic

If a 128 GB source contains only a few gigabytes of actual data, SD Cloner’s engine:

1. Reads used bytes via `df --output=used -B1` across mounted partitions.
2. Determines if data + margin ≤ destination capacity.
3. Creates a shrunken image with only used data, omitting empty sectors.
4. Writes a valid filesystem-sized image that fits a smaller card (e.g. 32 GB).

Passed validation:

 Test 1: 128 GB → 128 GB (Raw clone)
 Test 2: 128 GB → 32 GB (FS-aware shrink)

Screenshots

Insert screenshots of the main window, About dialog, and progress bar here.

Safety and Reliability Model

 Source is never modified — all mounts are read-only.
 Block devices only — GUI validates with `stat()` and `lsblk`.
 Unmount-before-write safeguard on destination.
 Comprehensive logging for every shell invocation.
 Explicit failure modes to prevent silent corruption.

Test Summary

| Test | Description                     | Result                               |
| - | - |  |
| 1    | 128 GB to 128 GB clone          | ✅ Passed                             |
| 2    | 128 GB image → 32 GB (data-fit) | ✅ Passed                             |
| 3    | Burn `.img` and `.img.gz`       | ✅ Passed                             |
| 4    | Overwrite protection            | ✅ Passed (engine refused unsafe ops) |

Roadmap

 Partition map mirroring (multi-partition shrink)
 Real-time byte-progress parsing (libparted integration)
 Hidden non-removable disks
 Optional write verification via checksum
 Dark theme + i18n (GTK theming & gettext)

Developer Notes

 Engine/GUI separation: all imaging logic resides in the engine.
 The GUI interacts through exported API calls (`sdcloner_clone()`, `burn_image_to_disk()`).
 The `main.c` CLI tool is optional for headless or automated workflows.
 Designed for extensibility: new frontends (Qt or CLI++) can reuse the same engine.

About

Author: Dr. Eric Oliver Flores Toro
Version: 1.0 (October 2025)
License: GPL v3
Target Platform: PicoCalc / Pop!_OS 22.04 LTS
Technologies: C17 · GTK3 · dd · gzip · rsync · parted · losetup · blkid · lsblk

License

This project is licensed under the GNU General Public License v3 (GPLv3).
See the `LICENSE` file for details.

Acknowledgments

Special thanks to the ClockworkPi / PicoCalc community for maintaining open access to firmware resources that made this project possible.

```
