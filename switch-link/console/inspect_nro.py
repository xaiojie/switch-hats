"""Inspect actual NRO header, embedded icon, app metadata and RomFS."""
import hashlib, json, struct, sys
from pathlib import Path

path = Path(sys.argv[1])
data = path.read_bytes()
assert data[16:20] == b'NRO0', 'Invalid NRO magic'
program_size = struct.unpack_from('<I', data, 24)[0]
assert data[program_size:program_size+4] == b'ASET', 'Missing asset header'
icon_off, icon_size, nacp_off, nacp_size, romfs_off, romfs_size = struct.unpack_from('<6Q', data, program_size+8)
assert nacp_size == 0x4000, 'Invalid NACP size'
for offset, size in [(icon_off,icon_size),(nacp_off,nacp_size),(romfs_off,romfs_size)]:
    assert size > 0 and program_size+offset+size <= len(data), 'Asset bounds error'
nacp = data[program_size+nacp_off:program_size+nacp_off+nacp_size]
name = nacp[:512].split(b'\0',1)[0].decode()
author = nacp[512:768].split(b'\0',1)[0].decode()
assert name.startswith('Switch Link'), 'Incorrect app identity'
version = nacp[0x3060:0x3070].split(b'\0',1)[0].decode()
icon = data[program_size+icon_off:program_size+icon_off+icon_size]
assert icon[:2] == b'\xff\xd8', 'Invalid JPEG icon'
report = {'file':path.name, 'size':len(data), 'sha256':hashlib.sha256(data).hexdigest(), 'app_name':name, 'version':version,
          'author':author, 'program_size':program_size, 'icon_size':icon_size, 'nacp_size':nacp_size,
          'romfs_size':romfs_size, 'hardware_tested':False}
print(json.dumps(report, indent=2))
path.with_suffix('.build-info.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
