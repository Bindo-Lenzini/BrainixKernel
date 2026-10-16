#!/usr/bin/env python3
# mkmedia.py: упаковать медиа-файлы (картинки) в raw-образ media.img
# для BKOS. Формат MEDI, 512-байтные сектора.

import os
import sys
import struct

SECTOR    = 512
MAGIC     = b'MEDI'
VERSION   = 1
MAX_FILES = 120

def pack(src_dir, out_path):
    if not os.path.isdir(src_dir):
        print(f"[media] no such dir: {src_dir}", file=sys.stderr)
        sys.exit(1)

    files = []
    for name in sorted(os.listdir(src_dir)):
        if name.startswith('.'):
            continue
        p = os.path.join(src_dir, name)
        if not os.path.isfile(p):
            continue
        size = os.path.getsize(p)
        if size == 0:
            continue
        name_bytes = name.encode('utf-8')
        if len(name_bytes) > 47:
            print(f"[media] name too long (>47): {name}", file=sys.stderr)
            sys.exit(1)
        files.append((name_bytes, p, size))

    if len(files) > MAX_FILES:
        print(f"[media] too many files ({len(files)}), max {MAX_FILES}",
              file=sys.stderr)
        sys.exit(1)

    # 1 суперблок + 15 секторов таблицы = 16 секторов заголовка.
    header_sectors = 16

    entries = []
    cur = header_sectors
    for name_bytes, path, size in files:
        sectors = (size + SECTOR - 1) // SECTOR
        entries.append({
            'name':    name_bytes,
            'path':    path,
            'size':    size,
            'start':   cur,
            'sectors': sectors,
        })
        cur += sectors

    total_sectors = cur if cur > header_sectors else header_sectors

    with open(out_path, 'wb') as f:
        # Суперблок.
        sb = bytearray(SECTOR)
        sb[0:4] = MAGIC
        struct.pack_into('<I', sb, 4,  VERSION)
        struct.pack_into('<I', sb, 8,  len(entries))
        struct.pack_into('<I', sb, 12, header_sectors)
        f.write(sb)

        # Таблица файлов (15 секторов).
        table = bytearray(15 * SECTOR)
        for i, e in enumerate(entries):
            off = i * 64
            nm = e['name']
            table[off:off + len(nm)] = nm
            struct.pack_into('<I', table, off + 48, e['size'])
            struct.pack_into('<I', table, off + 52, e['start'])
        f.write(table)

        # Данные файлов.
        for e in entries:
            with open(e['path'], 'rb') as g:
                data = g.read()
            f.write(data)
            pad = e['sectors'] * SECTOR - len(data)
            if pad > 0:
                f.write(b'\0' * pad)

    print(f"[media] packed {len(entries)} files, {total_sectors} sectors "
          f"({total_sectors * SECTOR} bytes) -> {out_path}")

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <media_dir> <out.img>", file=sys.stderr)
        sys.exit(1)
    pack(sys.argv[1], sys.argv[2])