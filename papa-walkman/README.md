# 爬爬的随身听

一个独立的 Switch 本地 MP3 播放器。奶油白磁带机、橙色播放键、动态双卷轴，带中文曲库、触摸与手柄操作。

当前版本 **0.2.0** 是重新开发的基础版：实际 NRO 已编译，桌面核心测试及界面渲染检查通过；**真机声音与长时间播放尚待验证**。

## 放到 SD 卡

把发布包里的 `switch` 文件夹复制到卡根目录，或通过 Switch Link 的 FTP 文件管理上传：

```text
sdmc:/switch/papa-walkman/papa-walkman.nro
sdmc:/music/歌手 - 歌曲.mp3
```

音乐放 `music`，允许子文件夹，最多八层和 10000 首。推荐文件名为 `歌手 - 歌曲.mp3`；其他文件名也能播放。当前曲名来自文件名，不读取专辑标签或封面。

按住 R 启动一款游戏进入自制菜单，再打开“爬爬的随身听”。启动后选一首歌开始播放；这是前台应用，按 HOME/休眠后的行为应以真机验证为准。

## 基础功能

- MP3 流式解码，48kHz 双声道输出，单声道与常见采样率自动转换。
- 中文曲库、歌曲选择、上一首/下一首、暂停/继续。
- 进度条触摸跳转、十秒快进/快退、音量。
- 顺序播放、列表循环、单曲循环、随机播放；随机一轮内不重复。
- 播放中卷轴旋转、暂停时停止；空曲库、扫描和错误均有提示。
- X 后台重新扫描，不打断当前歌曲；退出保存音量和播放模式。

第一版只支持 MP3。FLAC/WAV/OGG、联网音乐、m3u 歌单、后台常驻与断点续播没有包含在本版。

| 操作 | 按键 |
|---|---|
| 选择歌曲 | ↑ / ↓ |
| 播放选中歌曲；选中当前歌曲时暂停/继续 | A |
| 上一首 / 下一首 | L / R |
| 后退 / 前进十秒 | ZL / ZR |
| 音量减少 / 增加 | ← / → |
| 切换播放模式 | Y |
| 重新扫描 | X |
| 保存并退出 | + |

歌曲列表、播放键、切歌、模式、音量、进度条也支持触摸；在列表内上下滑动可滚动。

配置位置：`sdmc:/config/papa-walkman/settings.ini`。中文使用系统共享字体，无须另装字体文件。

## 编译

与之前 Switch Link 一样，使用 **devkitA64 + libnx**。本版没有 SDL2 依赖，随仓库提供 minimp3 和 stb；图形直接写 libnx framebuffer，声音使用 audout。

本机便携工具链：devkitA64 GCC 16.1.0，CMake + Ninja。Windows：

```powershell
./build.ps1 -ToolchainRoot '你的便携工具链目录'
```

工具链目录应包含 `root/opt/devkitpro`、`cmake/cmake-4.4.5-windows-x86_64/bin/cmake.exe` 和 `ninja/ninja.exe`，与仓库内 Switch Link 的便携构建布局相同。脚本会在工具链旁的 ASCII 路径暂存和构建，产物复制回 `build/papa-walkman.nro`。

Linux / 已配置 devkitPro 的环境：

```bash
./build.sh
```

需提供 `assets/icon.jpg`（仓库已包含）。高分辨率图标母图是 `assets/icon-master.png`；Windows 构建会生成对应 256×256 JPEG。

## 测试与界面预览

```powershell
./tools/test.ps1 -Compiler g++
```

测试使用真实 MP3 解码、真实队列/重采样与可控音频输出端，验证暂停缓冲、进度、跳转、结束排空和输出失败；还验证扫描、配置、触摸与绘制边界。`tests/tone.mp3` 是原创合成正弦音测试样本；需要重新生成时使用 `tools/make_fixture.py`，依赖 `lameenc==1.9.0`。

`tools/preview.cpp` 使用与 Switch 相同的 UI 代码导出播放、暂停、空曲库、错误状态 PNG；示例曲库只用于预览，不含歌曲文件。

`tools/inspect_nro.py build/papa-walkman.nro` 检查 NRO 结构、中文标题、版本和嵌入图标。

## 工程结构

`core`：曲库/队列/配置/重采样；`decoder`：流式 MP3；`engine`：可测试的播放状态机；`audio`：工作线程与 Switch 音频缓冲；`ui`：同源绘制与触摸命中；`main`：输入、异步扫描与应用生命周期。

旧 0.1.0 应用实现已移除，完整旧工程归档保存在本机工作备份中。新版设计与计划见仓库 `docs/superpowers/`。
