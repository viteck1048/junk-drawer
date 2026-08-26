import struct, sys

def parse(path):
    d = open(path,'rb').read()
    assert d[0:4]==b'FILE' and d[8:12]==b'PFF2', "не PFF2"
    pos, meta, chix = 12, {}, None
    while pos < len(d)-8:
        name = d[pos:pos+4]; ln = struct.unpack('>I', d[pos+4:pos+8])[0]; pos += 8
        if name == b'DATA': break
        body = d[pos:pos+ln]
        if name == b'CHIX': chix = body
        elif name in (b'PTSZ',b'MAXW',b'MAXH',b'ASCE',b'DESC'):
            meta[name.decode()] = struct.unpack('>H', body)[0]
        elif name in (b'NAME',b'FAMI'):
            meta[name.decode()] = body.rstrip(b'\0').decode('utf8','replace')
        pos += ln
    cps = set()
    if chix:
        for i in range(0, len(chix)//9*9, 9):
            cps.add(struct.unpack('>I', chix[i:i+4])[0])
    return meta, cps

meta, cps = parse(sys.argv[1])
print("Назва      :", meta.get('NAME'))
print("PTSZ       :", meta.get('PTSZ'))
print("MAXW x MAXH:", meta.get('MAXW'), "x", meta.get('MAXH'), "px")
print("ascent/desc:", meta.get('ASCE'), "/", meta.get('DESC'))
print("гліфів     :", len(cps))

def rng(name, a, b):
    have = sum(1 for c in range(a,b+1) if c in cps)
    tot = b-a+1
    print(f"  {name:<22} {have}/{tot} {'OK' if have==tot else 'НЕПОВНО'}")

print("покриття:")
rng("Box Drawing",       0x2500, 0x257F)
rng("Block Elements",    0x2580, 0x259F)
rng("Кирилиця",          0x0400, 0x04FF)
rng("Latin-1 Supplement",0x00A0, 0x00FF)

print("\nукраїнські специфічні:")
for ch in "ҐґЄєІіЇї'АБВГДЕЖЗИЙКЛМНОПРСТУФХЦЧШЩЬЮЯабвгдежзийклмнопрстуфхцчшщьюя":
    if ord(ch) not in cps: print("  ВІДСУТНІЙ:", ch, hex(ord(ch)))
else: print("  усі присутні" if all(ord(c) in cps for c in "ҐґЄєІіЇїАЯая") else "")

print("\nяких 76 нема в 0400-04FF:")
miss=[c for c in range(0x0400,0x0500) if c not in cps]
print("  ", " ".join(chr(c) for c in miss[:40]))
