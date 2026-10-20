#!/usr/bin/python3
# Genera Devices.h (tabla del driver) y los fragmentos de personalidades IOKit
# a partir de devices.txt, para que driver e Info.plist no puedan divergir.
import sys

def load(path):
    rows = []
    for line in open(path):
        line = line.split('#')[0].strip()
        if not line:
            continue
        pid, gen, name = line.split()
        rows.append((int(pid, 16), gen, name))
    return rows

def header(rows, out):
    with open(out, 'w') as f:
        f.write('// Generado por scripts/gen_devices.py desde devices.txt. No editar.\n')
        f.write('#pragma once\n#include <stdint.h>\n')
        f.write('struct IbtDevice { uint16_t pid; uint8_t gen; };\n')
        f.write('static const IbtDevice kIbtDevices[] = {\n')
        for pid, gen, _ in rows:
            f.write('    { 0x%04X, %s },\n' % (pid, gen[-1]))
        f.write('};\n')

if __name__ == '__main__':
    header(load(sys.argv[1]), sys.argv[2])
