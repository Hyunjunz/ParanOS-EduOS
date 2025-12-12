clear
# Make ISO
make clean
make FONT_TTF=font/NotoSans-Medium.ttf FONT_SIZE=16 FONT_EMBOLDEN=0.6 FONT_CELL_W=10

# Select QEMU audio backend (sdl/pa/alsa/pipewire). Defaults to SDL (avoids PulseAudio volume errors on some setups).
AUDIO_BACKEND=${QEMU_AUDIO_BACKEND:-sdl}
case "$AUDIO_BACKEND" in
    sdl|pa|alsa|pipewire) ;;
    *) echo "Unknown QEMU_AUDIO_BACKEND '$AUDIO_BACKEND', falling back to 'sdl'"; AUDIO_BACKEND=sdl ;;
esac

# Optional: QEMU_AUDIO_DEBUG=1 to dump qemu-audio.log
AUDIO_OPTS="-audiodev ${AUDIO_BACKEND},id=aud,out.frequency=48000,out.channels=2 -device AC97,audiodev=aud"
if [ "${QEMU_AUDIO_DEBUG:-0}" = "1" ]; then
    AUDIO_OPTS="$AUDIO_OPTS -d audiodev -D qemu-audio.log"
fi

# Run
sudo setfacl -m u:$USER:rw /dev/kvm
qemu-system-x86_64 -m 1024M -machine q35 \
      -drive if=pflash,format=raw,readonly=on,file=OVMF_CODE.fd \
      -drive if=pflash,format=raw,file=OVMF_VARS.fd \
      -device piix3-ide,id=ide \
      -drive id=hd0,file=disk.img,if=none,format=raw \
      -device ide-hd,drive=hd0,bus=ide.0,bootindex=2 \
      -drive id=cd0,file=build/os.iso,if=none,format=raw,media=cdrom \
      -device ide-cd,drive=cd0,bus=ide.1,bootindex=1 \
      -nic none -accel kvm \
      $AUDIO_OPTS \
      -boot order=d,menu=on -serial stdio -vga std -display sdl
