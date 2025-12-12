#!/bin/bash

IMG=disk.img
SIZE=2G

# 1) 이미지 생성
qemu-img create -f raw $IMG $SIZE

# 2) 파티션 생성 (fdisk 자동 입력)
fdisk $IMG <<EOF
n
p
1


t
c
w
EOF

# 3) 루프 장치 연결
LOOP=$(sudo losetup -fP --show $IMG)

# 4) 파일시스템 포맷 (기본: exFAT, 필요 시 FS_TYPE=fat32 지정)
if [[ "${FS_TYPE:-exfat}" == "fat32" ]]; then
  if ! command -v mkfs.fat >/dev/null 2>&1; then
    echo "mkfs.fat가 필요합니다. exfat 대신 fat32를 쓰려면 mkfs.fat 설치 후 다시 시도하세요." >&2
    sudo losetup -d "$LOOP"
    exit 1
  fi
  sudo mkfs.fat -F 32 ${LOOP}p1
else
  if ! command -v mkfs.exfat >/dev/null 2>&1; then
    echo "mkfs.exfat이 없습니다. exfatprogs를 설치하거나 FS_TYPE=fat32로 설정하세요." >&2
    sudo losetup -d "$LOOP"
    exit 1
  fi
  sudo mkfs.exfat ${LOOP}p1
fi

# 5) 분리
sudo losetup -d $LOOP

echo "디스크 생성 완료 (${FS_TYPE:-exfat}): $IMG"
