#!/usr/bin/env python3
# Assembla core/biosrom.asm (nasm) e genera core/biosrom.h
import subprocess, os, sys
here = os.path.dirname(os.path.abspath(__file__))
core = os.path.join(here, '..', 'core')
subprocess.check_call(['nasm', '-f', 'bin', '-o', '/tmp/biosrom.bin', os.path.join(core, 'biosrom.asm')])
b = open('/tmp/biosrom.bin', 'rb').read()
assert len(b) == 0x2000, hex(len(b))
out = ['// Generato da tools/mkbiosrom.py da biosrom.asm - non modificare', '#pragma once',
       '#define BIOSROM_ORG 0xE000', 'static const unsigned char biosrom_bin[%d] = {' % len(b)]
for i in range(0, len(b), 16):
    out.append('  ' + ','.join('0x%02x' % x for x in b[i:i+16]) + ',')
out.append('};')
open(os.path.join(core, 'biosrom.h'), 'w').write('\n'.join(out) + '\n')
print('biosrom.h: %d byte' % len(b))
