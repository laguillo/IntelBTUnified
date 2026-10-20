#!/usr/bin/python3
# Empaqueta los firmwares (zlib) en FwBinary.cpp.
# uso: gen_fw.py <dir_fw> <salida.cpp> [patron_glob ...]
# Sin patrones se incluye todo; con patrones solo los ficheros que coincidan
# (p.ej. 'ibt-0041-*' para un AX201), lo que reduce mucho el tamano del kext.
import fnmatch, hashlib, os, sys, zlib

src, out = sys.argv[1], sys.argv[2]
pats = sys.argv[3:] or ['*']
files = sorted(f for f in os.listdir(src) if any(fnmatch.fnmatch(f, p) for p in pats))
if not files:
    sys.exit('gen_fw: ningun firmware coincide con %s' % pats)

def ident(s):
    return ''.join(c if c.isalnum() else '_' for c in s)

blobs, entries = {}, []
with open(out, 'w') as f:
    f.write('// Generado por scripts/gen_fw.py. No editar.\n#include "FwData.h"\n')
    for name in files:
        raw = open(os.path.join(src, name), 'rb').read()
        h = hashlib.sha1(raw).hexdigest()
        if h not in blobs:
            comp = zlib.compress(raw, 9)
            blobs[h] = (len(comp), len(raw))
            f.write('\nstatic const unsigned char fw_%s[] = {' % h)
            for i in range(0, len(comp), 16):
                f.write('\n' + ','.join('0x%02X' % b for b in comp[i:i + 16]) + ',')
            f.write('\n};\n')
        entries.append((name, h))
    f.write('\nconst struct FwDesc fwList[] = {\n')
    for name, h in entries:
        f.write('    { "%s", fw_%s, %d, %d },\n' % (name, h, blobs[h][0], blobs[h][1]))
    f.write('};\nconst int fwNumber = %d;\n' % len(entries))
print('gen_fw: %d ficheros, %d blobs unicos' % (len(entries), len(blobs)))
