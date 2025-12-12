#!/usr/bin/env bash
# QEMU with AC'97 on ALSA backend (out.dev=default). No sudo; TCG by default.
# Override: QEMU_ACCEL=kvm|tcg, AUDIO_OUT_DEV=<alsa device>

set -e

ACCEL=${QEMU_ACCEL:-tcg}
OUT_DEV=${AUDIO_OUT_DEV:-default}

qemu-system-x86_64 -m 1024M -machine pc \
  -drive if=pflash,format=raw,readonly=on,file=OVMF_CODE.fd \
  -drive if=pflash,format=raw,file=OVMF_VARS.fd \
  -device piix3-ide,id=ide \
  -drive id=hd0,file=disk.img,if=none,format=raw \
  -device ide-hd,drive=hd0,bus=ide.0,bootindex=2 \
  -drive id=cd0,file=build/os.iso,if=none,format=raw,media=cdrom \
  -device ide-cd,drive=cd0,bus=ide.1,bootindex=1 \
  -nic none -accel "$ACCEL" \
  -audiodev "alsa,id=aud,out.frequency=48000,out.channels=2,out.dev=${OUT_DEV}" \
  -device AC97,audiodev=aud \
  -boot order=d,menu=on -serial stdio -vga std -display sdl
