# VAM OS 🧠  

### (그냥 Readme 는 GPT 시켰습니다. 귀찮았어요.)

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

## ⚙️ Requirements (Ubuntu / Debian)
```
sudo apt update
sudo apt install -y build-essential gcc-multilib nasm qemu-system-x86 grub-pc-bin xorriso mtools make git
```

---

## 🏗️ Build Instructions
```
make
make run
```

---

## ⚖️ License
Licensed under **Creative Commons BY-NC-SA 4.0**  
Non-commercial use only, attribution required, share alike.  
```
https://creativecommons.org/licenses/by-nc-sa/4.0/
```

---

## 👨‍💻 Author
**현준 (HyunJun)** — 15-year-old student developer  
Developed VAM OS alone in 2 months to explore OS internals.  
Interested in Operating Systems, Low-Level Programming, and AI.  
GitHub: [https://github.com/Hyunjunz]([https://github.com/Hyunjunz])

---

---

# 🇰🇷 VAM OS (한국어)

**VAM OS**는 15살 개발자가 C와 어셈블리로 직접 만든 32비트 x86 커널입니다.  
2개월 동안 운영체제의 부팅 과정, 메모리 관리, 태스크 스위칭 등을 배우기 위해 개발되었습니다.

---

## 🧩 주요 기능
- GRUB (Multiboot2) 부팅 지원  
- 32비트 보호 모드 커널  
- GDT / IDT / ISR / PIC / PIT 초기화  
- 물리/가상 메모리 관리 (PMM, VMM)  
- 시리얼 포트 및 PS/2 키보드 드라이버  
- 시스템 콜 인터페이스  
- 간단한 태스크 스위칭 및 멀티태스킹  
- PSF 폰트를 이용한 프레임버퍼 콘솔 렌더링  

---

## 🧠 프로젝트 소개
VAM OS는 “운영체제는 어떻게 작동할까?”라는 궁금증에서 시작되었습니다.  
2개월 동안 다음과 같은 부분을 직접 구현하며 배웠습니다:
- 부트로더가 커널로 제어를 넘기는 과정  
- 인터럽트 처리(IDT, ISR) 구조  
- 페이지 기반 메모리 관리  
- 사용자 모드와 커널 모드 전환  

아직 완성된 운영체제는 아니지만, 시스템 구조를 깊이 이해하는 큰 계기가 되었습니다.

---

## ⚙️ 빌드 환경 (Ubuntu / Debian)
```
sudo apt update
sudo apt install -y build-essential gcc-multilib nasm qemu-system-x86 grub-pc-bin xorriso mtools make git
```

---

## 🏗️ 빌드 및 실행
```
make
make run
```

QEMU에서 부팅 후 커널 초기화 로그가 출력됩니다.

---

## ⚖️ 라이선스
이 프로젝트는 **CC BY-NC-SA 4.0 (비영리-출처표시-동일조건)** 라이선스로 배포됩니다.  
출처를 표시해야 하며, 상업적 사용은 불가능하고, 수정 시 동일한 조건으로 공유해야 합니다.

```
https://creativecommons.org/licenses/by-nc-sa/4.0/
```

---

## 👨‍💻 제작자
- 이름: **현준 (HyunJun)**  
- 나이: 15살  
- 제작 기간: 약 2개월  
- 관심 분야: 운영체제, 시스템 프로그래밍, AI  
- GitHub: [https://github.com/Hyunjunz]([https://github.com/Hyunjunz])

---

---

# 🇯🇵 VAM OS (日本語)

**VAM OS** は、15歳の開発者が C とアセンブリで作成した 32ビット x86 カーネルです。  
2ヶ月間かけて、OSの起動プロセス、メモリ管理、タスクスイッチングなどを学ぶために開発されました。

---

## 🧩 主な機能
- GRUB (Multiboot2) 対応ブート  
- 32ビット保護モードカーネル  
- GDT / IDT / ISR / PIC / PIT 初期化  
- 物理 / 仮想メモリ管理 (PMM / VMM)  
- シリアル I/O と PS/2 キーボードドライバ  
- システムコールインターフェース  
- シンプルなタスクスイッチングとマルチタスク  
- フレームバッファコンソール用 PSF フォント描画  

---

## 🧠 プロジェクトについて
VAM OS は「OSはどのように動作するのか？」という疑問から始まりました。  
2ヶ月の開発を通じて、以下のようなことを学びました：
- ブートローダがカーネルに制御を渡す仕組み  
- 割り込み処理 (IDT / ISR)  
- ページングとメモリアロケータ  
- ユーザーモードとカーネルモードの切り替え  

まだ完成したOSではありませんが、システムアーキテクチャの理解に大きく役立ちました。

---

## ⚙️ ビルド環境 (Ubuntu / Debian)
```
sudo apt update
sudo apt install -y build-essential gcc-multilib nasm qemu-system-x86 grub-pc-bin xorriso mtools make git
```

---

## 🏗️ ビルドと実行
```
make
make run
```

---

## ⚖️ ライセンス
本プロジェクトは **CC BY-NC-SA 4.0 (表示・非営利・継承)** ライセンスの下で公開されています。  
出典の明記が必要であり、商用利用は禁止されています。  
また、改変・再配布する場合は同じ条件を維持する必要があります。

```
https://creativecommons.org/licenses/by-nc-sa/4.0/
```

---

## 👨‍💻 作者
- 名前: **ヒョンジュン (HyunJun)**  
- 年齢: 15歳  
- 開発期間: 約2ヶ月  
- 興味分野: オペレーティングシステム、低レベルプログラミング、AI  
- GitHub: [https://github.com/Hyunjunz]([https://github.com/Hyunjunz])

---

> 💬 “I made VAM OS at 15 to truly understand how a computer boots,  
> manages memory, and switches between tasks — from zero.”
