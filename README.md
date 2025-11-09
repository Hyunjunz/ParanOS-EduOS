# VAM OS 🧠  
A lightweight 32-bit x86 kernel written in C and Assembly.  
Developed **solo by a 15-year-old student** over the course of **2 months** —  
as a personal challenge to learn how computers work at the lowest level.

---

## 🧩 Features
- Bootable with GRUB (Multiboot2)
- 32-bit protected mode kernel
- GDT / IDT / ISR / PIC / PIT initialization
- Physical & Virtual memory management (PMM / VMM)
- Basic serial I/O and PS/2 keyboard driver
- Syscall interface
- Simple task switching and multitasking support
- Custom PSF font rendering for framebuffer console

---

## 🧠 About This Project
VAM OS started as a small experiment to understand how an **operating system boots, manages memory, and runs user tasks**.  
During 2 months of development, I learned:
- How bootloaders hand control to the kernel  
- How interrupt handling (IDT / ISR) works  
- How paging and memory allocators are implemented  
- How to switch between user and kernel mode safely  

It’s not meant to be a full OS yet — but it’s a big step toward understanding system architecture at a low level.

---

## 🧱 Build System
The kernel uses a custom Makefile-based build system.  
It supports:
- NASM + GCC cross-compilation  
- GRUB ISO generation  
- QEMU run/debug targets  

---

## ⚙️ Requirements (Ubuntu / Debian)
```
sudo apt update
sudo apt install -y build-essential gcc-multilib nasm qemu-system-x86 grub-pc-bin xorriso mtools make git
```

---

## 🏗️ Build Instructions
Clone and build the kernel:
```
git clone https://github.com/<yourname>/vam-os.git
cd vam-os
make
```

To run in QEMU:
```
make run
```
To debug:
```
make debug
```

---

## 🧰 File Structure
```
VAM/
 ├─ kernel/              # Core kernel source (C, ASM, headers)
 │   ├─ mm/              # Memory management
 │   ├─ task/            # Task switching & user mode
 │   ├─ panic/           # Panic / TSS fault handlers
 │   ├─ tss_panic/       # Double-fault recovery
 │   ├─ include/         # Common headers
 │   └─ ...
 ├─ font/                # PSF font used for console rendering
 ├─ Makefile             # Build rules
 ├─ run.sh               # Build & run helper script
 └─ README.md
```

---

## 💿 Boot & Run Example
After successful build:
```
✅ ISO built successfully: build/os.iso
🗺  Linker map: build/kernel.map
```

Run it with:
```
qemu-system-i386 -cdrom build/os.iso -serial stdio -m 256M -vga vmware
```

You should see:
```
Boot Complete
[Kernel] syscall ready.
[User] entry=...
```

---

## ⚖️ License
This project is licensed under **Creative Commons BY-NC-SA 4.0**.  
You may use and modify this code for **non-commercial purposes**,  
but you must give credit to the author and share any derivative works under the same license.

```
VAM OS - Copyright (c) 2025 Hyunjunz
Licensed under CC BY-NC-SA 4.0  
https://creativecommons.org/licenses/by-nc-sa/4.0/
```

---

## 👨‍💻 Author
- **현준 (HyunJun)** — 15-year-old student developer  
- Interested in: Operating Systems, Low-level Programming, and AI systems  
- Developed VAM OS over **2 months** as a personal learning project  
- ✉️ [GitHub Profile]([https://github.com/Hyunjunz])

---

> 🧡 “I built VAM OS from scratch to understand how a computer actually runs code —  
> from bootloader to multitasking. It was one of the most challenging, but most rewarding projects I’ve ever done.”
