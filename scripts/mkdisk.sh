#!/bin/bash
# scripts/mkdisk.sh — создаёт disk.img (виртуальный накопитель для BKFS),
# если его ещё нет. Размер — 2048 МБ (можно менять).
#
# ВАЖНО: если disk.img уже существует и содержит файлы BKFS — скрипт
# НЕ трогает его. Чтобы расширить существующий, удали вручную и пересоздай
# (данные в ФС будут потеряны — они не критичны).

set -e

DISK="disk.img"
SIZE_MB=2048

if [ ! -f "$DISK" ]; then
    echo "[disk] creating $DISK (${SIZE_MB} MB)..."
    dd if=/dev/zero of="$DISK" bs=1m count="$SIZE_MB" 2>/dev/null
    echo "[disk] done: $(du -h $DISK | cut -f1)"
else
    echo "[disk] $DISK already exists: $(du -h $DISK | cut -f1)"
fi