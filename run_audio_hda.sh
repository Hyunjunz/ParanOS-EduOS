#!/usr/bin/env bash
# QEMU with Intel HDA + hda-output (alternative to AC'97). No sudo; TCG by default.
# Override: QEMU_AUDIO_BACKEND=alsa|pa|sdl|pipewire|oss|jack|dbus|wav|none (default: alsa)
#           AUDIO_OUT_DEV=<alsa device> (when BACKEND=alsa), QEMU_ACCEL=kvm|tcg

set -e

BACKEND=${QEMU_AUDIO_BACKEND:-alsa}
case "$BACKEND" in
  alsa|pa|sdl|pipewire|oss|jack|dbus|wav|none) ;;
  *) echo "Unknown QEMU_AUDIO_BACKEND '$BACKEND', falling back to 'alsa'"; BACKEND=alsa ;;
esac

ACCEL=${QEMU_ACCEL:-tcg}
OUT_DEV_OPT=""
if [ -n "${AUDIO_OUT_DEV:-}" ]; then
  OUT_DEV_OPT=",out.dev=${AUDIO_OUT_DEV}"
elif [ "$BACKEND" = "alsa" ]; then
  OUT_DEV_OPT=",out.dev=default"
fi

qemu-system-x86_64 -m 1024M -machine q35 \
  -drive if=pflash,format=raw,readonly=on,file=OVMF_CODE.fd \
  -drive if=pflash,format=raw,file=OVMF_VARS.fd \
  -device piix3-ide,id=ide \
  -drive id=hd0,file=disk.img,if=none,format=raw \
  -device ide-hd,drive=hd0,bus=ide.0,bootindex=2 \
  -drive id=cd0,file=build/os.iso,if=none,format=raw,media=cdrom \
  -device ide-cd,drive=cd0,bus=ide.1,bootindex=1 \
  -nic none -accel "$ACCEL" \
  -audiodev "${BACKEND},id=aud,out.frequency=48000,out.channels=2${OUT_DEV_OPT}" \
  -device ich9-intel-hda -device hda-output,audiodev=aud \
  -boot order=d,menu=on -serial stdio -vga std -display sdl
