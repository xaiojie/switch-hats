"""Validate the actual NRO sections, UTF-8 NACP and 256x256 JPEG icon."""
import hashlib
import json
import pathlib
import struct
import sys
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('path', type=pathlib.Path)
parser.add_argument('--version', default='0.3.0')
args = parser.parse_args()
path = args.path
data = path.read_bytes()
assert data[0x10:0x14] == b'NRO0', 'NRO magic missing'
program_size = struct.unpack_from('<I', data, 0x18)[0]
assert data[program_size:program_size+4] == b'ASET', 'Asset header missing'
sections = {}
for name, field in [('icon', 8), ('nacp', 24), ('romfs', 40)]:
    offset, size = struct.unpack_from('<QQ', data, program_size+field)
    assert program_size+offset+size <= len(data), f'{name} outside file'
    sections[name] = data[program_size+offset:program_size+offset+size]
nacp = sections['nacp']
assert len(nacp) == 0x4000, 'Unexpected NACP length'
title = nacp[:512].split(b'\0')[0].decode('utf-8')
author = nacp[512:768].split(b'\0')[0].decode('utf-8')
version = nacp[0x3060:0x3070].split(b'\0')[0].decode('utf-8')
assert title == '爬爬的随身听', title
for language in range(16):
    assert nacp[language*768:language*768+512].split(b'\0')[0].decode('utf-8') == title
assert version == args.version, version
icon = sections['icon']
assert icon[:2] == b'\xff\xd8', 'Icon is not JPEG'
pos, dimensions = 2, None
while pos+4 < len(icon):
    assert icon[pos] == 0xff
    marker = icon[pos+1]
    length = struct.unpack_from('>H', icon, pos+2)[0]
    if marker in (0xc0, 0xc1, 0xc2):
        dimensions = struct.unpack_from('>HH', icon, pos+5)
        break
    pos += 2+length
assert dimensions == (256, 256), dimensions
print(json.dumps(dict(file=path.name, size=len(data), sha256=hashlib.sha256(data).hexdigest(),
                     title=title, author=author, version=version, icon_size=len(icon),
                     icon_dimensions=dimensions, program_size=program_size,
                     hardware_tested=False), ensure_ascii=False, indent=2))
