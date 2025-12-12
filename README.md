# ParanOS

Limine 기반 x86_64 취미 OS 커널. BIOS/UEFI 모두 부팅 가능하며, 프레임버퍼 GUI(라이트 테마), 기본 창 관리자, FAT32/ISO9660 파일시스템, AC'97 오디오, 간단한 태스크/시스템 콜, ATA·키보드·마우스 드라이버를 포함합니다. 핵심 빌드 산출물은 `build/os.iso`.

## 주요 기능
- **부트 경로**: Limine 요청/구성(`limine.c`, `limine_requests.c`, `limine.conf`), BIOS/UEFI 양쪽 지원. Multiboot1/2, Linux 커널 체인로더, PXE/TFTP 부트 스텁(`protos/`, `pxe/`).
- **그래픽/UI**: 프레임버퍼 백/프론트 버퍼(`fb.c`), 플랜텀 터미널, 라이트 톤 데스크톱/태스크바/아이콘 캐시(`desktop.c`, `wm.c`, `ui.c`), 라운드 표면·그라데이션 헬퍼, 배경/월페이퍼 로딩.
- **입력/인터럽트**: PS/2 키보드·마우스(`keyboard.c`, `mouse.c`), IDT/ISR/GDT/TSS/PIC/PIT 설정, 간단한 스케줄러·태스크 컨텍스트 스위치(`task/`), 시스템 콜 진입(`syscall.c`).
- **스토리지/FS**: ATA PIO(`ata.c`), MBR 파서, FAT32 읽기/쓰기(`fs_fat32.*`, `fs/`), ISO9660 읽기, 파일 추상화. 디스크 부트/미디어 테스트용 PXE/TFTP 지원 코드.
- **메모리/커널 서비스**: PMM/VMM(HHDM) 초기화(`mm/`), 커널 힙 `kmalloc`, 간단한 물리 연속 할당기. 플랫 디바이스 트리(libfdt) 유틸 포함.
- **오디오**: AC'97 DMA 드라이버(`drivers/ac97.c`)와 WAV(PCM) 파서/재생 경로(`wav.c`, `kernel.c` 내 resampler).
- **라이브러리**: ELF/PE 파서, GUID/URI/이미지/STB 이미지, 시간·랜덤·터미널·트레이스·패닉 헬퍼(`lib/`, `libc-compat/`).

## 리포지토리 구조
- `kernel/`: 커널 진입(`kernel_entry.asm` → `kernel.c`), 그래픽/UI, 입력, 메모리, 드라이버(ATA/AC97/VBE/GOP/시리얼 등), 파일시스템(FAT32/ISO9660), 부트 프로토콜(`protos/`), PXE/TFTP, 스케줄러(`task/`), 시스템 코드(`sys/`), FDT·lib 유틸.
- `boot/`: BIOS 부트 스테이지 어셈블리.
- `limine/`: 포함된 Limine 바이너리(ISO 생성 시 사용).
- `UEFI/`: Limine 소스/UEFI 관련 자료(부트로더 빌드 참고용).
- `docs/`: 커널 개요, UI·오디오 가이드, exFAT 예제 코드 등.
- `tools/`, `font/`, `Cursor/`: 폰트/커서/유틸 소스.
- 스크립트: `run.sh`(빌드+실행), `no-compile-run.sh`(실행만), `run_audio_*.sh`(오디오 백엔드별 QEMU), `run_tcg.ps1`(Windows TCG), `create_disk.sh`(가상 디스크 생성).

## 빌드 준비물
- Linux 환경 기준: `gcc`, `nasm`, `ld`, `objcopy`, `xorriso`, `qemu-system-x86_64`, `ovmf`(또는 `OVMF_CODE.fd`/`OVMF_VARS.fd` 경로 준비), `make`.
- 오디오 없는 실행도 가능하지만, AC'97 테스트 시 QEMU가 오디오 백엔드를 지원해야 합니다(SDL/PA/ALSA/pipewire 등).
- `create_disk.sh` 사용 시 `mkfs.exfat`(기본) 또는 `mkfs.fat` 필요.

## 빌드
```bash
make                         # kernel.elf + build/os.iso 생성
make clean                   # 빌드/ISO 정리

# 커스텀 TTF 폰트 예시
make FONT_TTF=font/NotoSans-Medium.ttf FONT_SIZE=16 FONT_EMBOLDEN=0.6 FONT_CELL_W=10
```
- 산출물: `build/kernel.elf`, `build/os.iso`, `build/kernel.map`, `iso_root/`(임시 ISO 구성물).
- 링크 스크립트: `kernel/linker.ld`, 커널 옵션은 `config.h` 참고.

## 실행 (QEMU)
```bash
# 빌드+실행
./run.sh                     # QEMU_AUDIO_BACKEND=sdl|pa|alsa|pipewire

# 기존 ISO로 바로 실행
./no-compile-run.sh
```
- 기본 옵션: q35 + KVM, AC'97 오디오, OVMF UEFI 펌웨어, `disk.img`(가상 디스크) + `build/os.iso`(CD).
- 오디오 디버그: `QEMU_AUDIO_DEBUG=1 ./run.sh` → `qemu-audio.log` 생성.
- Windows TCG: `run_tcg.ps1`(백엔드 dsound/sdl/pa).
- 부트 설정은 `limine.conf`에서 조정.

## 디스크 이미지 준비 (옵션)
```bash
./create_disk.sh             # 2G raw, exFAT 기본 생성
FS_TYPE=fat32 ./create_disk.sh
```
- `disk.img`에 파티션 생성 후 exFAT/FAT32로 포맷합니다. 루프 디바이스/포맷에 sudo 권한이 필요합니다.

## 개발 메모
- 커널 초기화 흐름: `kernel_entry.asm` → `kmain`(`kernel.c`) → GDT/IDT/PIC/PIT → PMM/VMM → 프레임버퍼/폰트 로드 → 태스크/시스템콜/입출력/파일시스템 초기화 → 데스크톱/태스크바 렌더 루프 진입.
- UI 색상·테마는 `kernel.c`, `desktop.c`, `wm.c`, `ui.c` 상단 상수로 관리합니다.
- AC'97 재생 경로: `sound_play_wav_path`(`kernel.c`) → `wav_parse` → `ac97_play_pcm`; 시리얼 로그로 파라미터 확인 가능.
- FAT32 경로 기반 API: `fat32_read_file_path`, `fat32_write_file_path`; MBR 파싱은 `fs_mbr.c`.

## 참고 문서
- `docs/kernel-overview-ko.md`: 커널 소스 역할/흐름 요약.
- `docs/ui-theme-and-audio.md`: 라이트 UI/오디오 백엔드 메모.
- `docs/exfat_portfolio.c`: exFAT 디렉터리 파싱 예제.
