#!/bin/bash
# =====================================================
#  VAM OS Build Environment Setup (for Ubuntu/Debian)
# =====================================================

set -e

echo "🔧 Updating package list..."
sudo apt update

echo "📦 Installing required packages..."
sudo apt install -y \
    build-essential \
    gcc-multilib \
    nasm \
    qemu-system-x86 \
    grub-pc-bin \
    xorriso \
    mtools \
    make \
    git

echo "✅ All required packages installed successfully!"
echo "🧠 You can now build with: make or ./run.sh"
