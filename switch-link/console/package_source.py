"""Bundle the exact modified console source and build-time library sources."""
import json
from pathlib import Path
import subprocess
import sys
import zipfile

base = Path(__file__).resolve().parent
upstream = Path(sys.argv[1]).resolve()
libnx = Path(sys.argv[2]).resolve()
dest = base / 'switch-link-console-source.zip'
deps = upstream / 'build/SwitchLink/_deps'
toolchain = upstream.parent.parent / 'toolchain'

def revision(path):
    return subprocess.check_output(['git', '-C', str(path), 'rev-parse', 'HEAD'], text=True).strip()

lock = {'sphaira': {'url':'https://github.com/NaGaa95/sphaira', 'commit':revision(upstream)},
        'libnx': {'url':'https://github.com/switchbrew/libnx', 'commit':revision(libnx)}, 'dependencies':{}}
if (base / 'switch-link.build-info.json').exists():
    lock['app_version'] = json.loads((base / 'switch-link.build-info.json').read_text()).get('version')
for path in sorted(deps.glob('*-src')):
    lock['dependencies'][path.name[:-4]] = revision(path)
(base / 'source-lock.json').write_text(json.dumps(lock, indent=2), encoding='utf-8')

def add_tree(archive, source, prefix, excluded):
    for path in source.rglob('*'):
        if not path.is_file(): continue
        relative = path.relative_to(source)
        if any(part in excluded for part in relative.parts): continue
        archive.write(path, str(Path(prefix) / relative))

with zipfile.ZipFile(dest, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
    add_tree(archive, upstream, 'sphaira', {'.git', 'build', '.vscode'})
    add_tree(archive, libnx, 'libnx', {'.git', 'release', 'debug', 'lib', 'docs', 'internal_docs'})
    for path in sorted(deps.glob('*-src')):
        add_tree(archive, path, 'third_party/' + path.name[:-4], {'.git', 'tests', 'test', 'examples', '__pycache__'})
    # The exact portlib source releases, platform patches and licenses accompany
    # the GPL executable as well as libraries fetched by the console project.
    add_tree(archive, toolchain / 'portlib-sources', 'portlib-sources', set())
    for name in ['deko3d', 'switch/curl', 'switch/zlib', 'switch/mbedtls', 'switch/glm']:
        add_tree(archive, upstream.parent / 'pacman-packages' / name, 'portlib-recipes/' + name, {'.git'})
    add_tree(archive, toolchain / 'root/opt/devkitpro/portlibs/switch/licenses', 'licenses/portlibs', set())
    add_tree(archive, toolchain / 'root/opt/devkitpro/portlibs/switch/include/glm', 'third_party/glm', set())
    for name in ['README.md', 'LICENSE', 'VALIDATION.md', 'source-lock.json', 'toolchain-packages.json', 'apply_customization.py', 'build.ps1', 'build-offline.sh', 'inspect_nro.py', 'package_source.py', 'switch-link.build-info.json']:
        if (base / name).exists(): archive.write(base / name, 'switch-link/' + name)
    add_tree(archive, base / 'overlay', 'switch-link/overlay', {'__pycache__'})
    add_tree(archive, base / 'tests', 'switch-link/tests', {'__pycache__'})
print(f'Created {dest.name}: {dest.stat().st_size:,} bytes')
