"""Package a verified local build without runtime data, build products or system fonts."""
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import zipfile

root = pathlib.Path(__file__).resolve().parents[1]
release = root / 'releases' / '0.2.0'
release.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, PYTHONIOENCODING='utf-8')
metadata = json.loads(subprocess.check_output(
    [sys.executable, str(root/'tools/inspect_nro.py'), str(root/'build/papa-walkman.nro')],
    encoding='utf-8', env=env))
shutil.copy2(root/'build/papa-walkman.nro', release/'papa-walkman.nro')
metadata['compiler'] = 'devkitA64 GCC 16.1.0'
metadata['renderer'] = 'libnx framebuffer / shared system fonts / stb_truetype'
metadata['audio'] = 'libnx audout / streaming minimp3 / 48000Hz stereo'
metadata['software_validation'] = ['core queue/library/resampling/settings', 'real MP3 decoder/seek/cancel',
                                   'playback engine/pause/seek/EOF/output errors', 'UI hit targets/render bounds',
                                   'ARM64 NRO build and icon/NACP validation', 'same-code UI preview inspection']
metadata['hardware_validation'] = 'Not performed; sound quality, focus/sleep and sustained playback remain unverified.'
(release/'build-info.json').write_text(json.dumps(metadata, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
readme = '''# 爬爬的随身听 0.2.0

把安装包中的 switch 文件夹复制到 SD 卡根目录。也可以通过 FTP 放入：
sdmc:/switch/papa-walkman/papa-walkman.nro

MP3 歌曲放 sdmc:/music，支持子文件夹。推荐“歌手 - 歌曲.mp3”的文件名。
按住 R 启动一款游戏进入自制菜单，再打开“爬爬的随身听”。

↑↓ 选曲；A 播放选中歌曲/暂停当前歌曲；L/R 切歌；ZL/ZR 跳转十秒；
←→ 音量；Y 顺序/列表循环/单曲循环/随机；X 重扫曲库；+ 保存并退出。
支持触摸歌曲、播放/暂停、切歌、进度条、音量、模式与列表滑动。

第一版支持本地 MP3。曲名来自文件名，暂无专辑标签/封面、多格式、网络音乐、后台常驻或断点续播。
设置保存到 sdmc:/config/papa-walkman/settings.ini。

实际 ARM64 NRO 已编译，四组桌面功能测试、界面检查和嵌入图标/中文元数据检查通过。
真机声音、休眠/焦点切换、性能和长时间播放尚未验证。
预览使用同一 UI 绘制代码与示例曲名，不含真实歌曲，不是真机截图。

papa-walkman-source.zip 是对应源代码及构建/测试说明；SHA256.json 用于核对文件。
'''
(release/'README.md').write_text(readme, encoding='utf-8')
with zipfile.ZipFile(release/'papa-walkman-install.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    archive.write(release/'papa-walkman.nro', 'switch/papa-walkman/papa-walkman.nro')
    archive.writestr('README.txt', readme.encode('utf-8-sig'))
source_files=[]
for path in sorted(root.rglob('*')):
    if not path.is_file():
        continue
    relative=path.relative_to(root)
    if any(part in ('build', 'build-host', 'releases', '__pycache__', 'fixtures') for part in relative.parts):
        continue
    if path.suffix in ('.pyc', '.elf', '.nro', '.nacp', '.map', '.exe', '.keys', '.log'):
        continue
    source_files.append(path)
with zipfile.ZipFile(release/'papa-walkman-source.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    for path in source_files:
        archive.write(path, 'papa-walkman/'+path.relative_to(root).as_posix())
    for name in ('specs/2026-10-29-papa-walkman-rebuild-design.md', 'plans/2026-10-29-papa-walkman-rebuild.md'):
        path=root.parent/'docs/superpowers'/name
        if path.is_file(): archive.write(path, 'repository-docs/'+name)
previews=release/'previews'
previews.mkdir(exist_ok=True)
for name in ('playing.png', 'paused.png', 'empty.png', 'error.png'):
    shutil.copy2(root/'build-host/previews'/name, previews/name)
hashes={}
for path in sorted(release.rglob('*')):
    if path.is_file() and path.name!='SHA256.json':
        hashes[path.relative_to(release).as_posix()]=hashlib.sha256(path.read_bytes()).hexdigest()
(release/'SHA256.json').write_text(json.dumps(hashes, indent=2)+'\n', encoding='utf-8')
for name in ('papa-walkman-install.zip', 'papa-walkman-source.zip'):
    with zipfile.ZipFile(release/name) as archive:
        assert archive.testzip() is None
        assert all('..' not in pathlib.PurePosixPath(n).parts for n in archive.namelist())
print(json.dumps(dict(release=str(release), source_files=len(source_files), nro=metadata['sha256'],
                     source_zip=hashes['papa-walkman-source.zip']), ensure_ascii=False, indent=2))
