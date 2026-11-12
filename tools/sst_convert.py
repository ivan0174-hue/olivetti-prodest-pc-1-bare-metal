#!/usr/bin/env python3
# Converte i test SingleStepTests/8088 v1 (JSON) in un formato testo semplice per host/sst_test
import json, gzip, sys, os
src, dst, limit = sys.argv[1], sys.argv[2], int(sys.argv[3])
meta = json.load(open(os.path.join(src, 'metadata.json')))['opcodes']
R = ['ax','bx','cx','dx','cs','ss','ds','es','sp','bp','si','di','ip','flags']
for fn in sorted(os.listdir(src)):
    if not fn.endswith('.json.gz'): continue
    key = fn[:-8]
    parts = key.split('.')
    m = meta.get(parts[0], {})
    if len(parts) > 1: m = m.get('reg', {}).get(parts[1], {})
    mask = m.get('flags-mask', 0xFFFF)
    status = m.get('status', 'normal')
    tests = json.load(gzip.open(os.path.join(src, fn)))[:limit]
    with open(os.path.join(dst, key + '.txt'), 'w') as f:
        f.write('%s %d %s %d\n' % (key, mask, status, len(tests)))
        for t in tests:
            ir = t['initial']['regs']; fr = dict(ir); fr.update(t['final']['regs'])
            f.write(' '.join(str(ir[k]) for k in R) + '\n')
            f.write(' '.join(str(fr[k]) for k in R) + '\n')
            iram = t['initial']['ram']; fram = t['final']['ram']
            f.write(str(len(iram)) + ' ' + ' '.join('%d %d' % (a, v) for a, v in iram) + '\n')
            f.write(str(len(fram)) + ' ' + ' '.join('%d %d' % (a, v) for a, v in fram) + '\n')
