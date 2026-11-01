# 爬爬的随身听

一个独立的 Switch MP3 播放器。提供唱片内页、夜航唱片、歌词剧场三套主题，带中文曲库、封面、歌词、触摸与手柄操作。

当前源码版本为 **0.5.1 测试版**。在三套主题的基础上统一曲库、设置与播放页的布局，设置按用途分组，并加入平滑滚动歌词。0.5.1 已原位部署并回读校验，等待新版真机操作与滚动体验验证；详情见 `docs/validation-0.5.1.md`，部署状态以 `HANDOFF-CODEX.md` 顶部记录为准。

## 放到 SD 卡

把发布包里的 `switch` 文件夹复制到卡根目录，或通过 Switch Link 的 FTP 文件管理上传：

```text
sdmc:/switch/papa-walkman/papa-walkman.nro
sdmc:/music/歌手 - 歌曲.mp3
```

音乐放 `music`，允许子文件夹，最多八层和 10000 首。读取 ID3 曲名、歌手、专辑、内嵌封面和歌词；缺少曲名或歌手时使用文件名。推荐文件名为 `歌手 - 歌曲.mp3`。专辑分组依赖标签，当前不会把父文件夹名称当作专辑。

Switch Link 的中文 FTP 文件名保存问题仍在诊断中；Windows 的中文路径测试通过不代表主机 FTP 问题已经修复。可先通过读卡器复制保留原有目录结构；原始歌曲不会随源码提交。

已有桌面图标与 forwarder 保持原样，更新时只覆盖同一路径的 NRO，完整回读校验后清空日志。仍从原来的图标启动；这是前台应用，按 HOME/休眠后的行为应以真机验证为准。

## 功能

- MP3 流式解码，48kHz 双声道输出，单声道与常见采样率自动转换。
- 中文曲库、歌曲选择、上一首/下一首、暂停/继续。
- 进度条触摸跳转、十秒快进/快退、音量。
- 顺序播放、列表循环、单曲循环、随机播放；随机一轮内不重复。
- 三套主题分别采用大封面内页、深色唱片、大字歌词布局，空曲库、扫描和错误均有提示。
- 曲库／播放／设置三个主入口；歌词与队列从播放页展开，支持歌曲、专辑、歌手分组与拼音首字母搜索。
- 所有页面共用居中导航与统一的主题样式；设置分为播放与外观、音乐来源、存储维护、歌单管理。
- SD 卡、云端、收藏、歌单分类；混合播放队列、排序、移除与 M3U8 导入导出。
- 自建 HTTP/HTTPS JSON 或 M3U 音乐源、流式播放、缓冲状态、有限磁盘缓存与上次播放恢复。
- JPEG/PNG 封面、LRC 时间轴歌词与纯文本歌词；支持歌词时间偏移。
- 同步歌词平滑向上滚动、当前句逐渐突出；切歌、跳转或切换主题会重置旧过渡，普通歌词不制造虚假时间轴。
- 后台扫描和元数据索引；退出保存设置，定期保存播放位置。

音频目前只支持 MP3，未包含 FLAC/WAV/OGG 或后台常驻。云端跳转和恢复取决于服务器 Range、时长信息及已缓存范围；不能恢复时会显示等待或错误信息。

| 操作 | 按键 |
|---|---|
| 切换曲库／播放／设置 | − |
| 选择歌曲／滚动歌词 | ↑ / ↓ |
| 播放选中歌曲；播放／歌词页暂停继续；设置页执行选中操作 | A |
| 返回上一级 | B |
| 上一首 / 下一首 | L / R |
| 播放页后退／前进十秒；曲库换来源；队列排序；设置切分组 | ZL / ZR |
| 音量减少／增加；曲库切来源；歌词页调整时间偏移 | ← / → |
| 曲库收藏（歌单总览删除歌单）；播放页开歌词；来源／维护页刷新 | Y |
| 曲库加入队列；播放页开队列；队列移除；来源页添加来源 | X |
| 设置页切换主题；曲库切歌曲／专辑／歌手分组 | 按下右摇杆 |
| 曲库搜索 | 按下左摇杆 |
| 保存并退出 | + |

在设置的「播放与外观」中直接点选主题；也可按下右摇杆，依次切换「唱片内页 → 夜航唱片 → 歌词剧场」。选择自动保存，切换时继续播放。

歌曲列表、播放键、切歌、模式、音量、进度条也支持触摸；在列表内上下滑动可滚动，进度条拖动预览、松手跳转。

配置目录：`sdmc:/config/papa-walkman/`，包括 `settings.ini`、`sources.json`、`state.json` 与 `resume.json`。中文使用系统共享字体，无须另装字体文件。音乐源也可在 Switch Link 网页的音乐页面编辑、同步；同步需要主机 FTP 可连接且没有正在进行的任务。

## 编译

与之前 Switch Link 一样，使用 **devkitA64 + libnx**。本版没有 SDL2 依赖，随仓库提供 minimp3、stb 和 yyjson；联网使用 devkitPro 的 libcurl/zlib，保留证书校验。图形直接写 libnx framebuffer，声音使用 audout。

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
./tools/test-host.ps1 -ToolchainRoot '你的便携工具链目录'
```

主机测试需要 Visual Studio 2022 C++ 工具、Git for Windows 的 libcurl DLL 和 Python。测试使用真实 MP3 解码、真实队列/重采样与可控音频输出端，验证暂停缓冲、进度、跳转、结束排空和输出失败；还验证标签、歌词、曲库状态、缓存、中文路径、触摸与绘制边界，以及本机受控 HTTP 服务的截断、取消和 Range。`tests/tone.mp3` 是原创合成正弦音测试样本；需要重新生成时使用 `tools/make_fixture.py`，依赖 `lameenc==1.9.0`。旧 `tools/test.ps1` 保留为基础版测试入口。

`tools/preview.cpp` 使用与 Switch 相同的 UI 代码导出三套主题及曲库、专辑、歌词、队列、设置、触摸和缓冲状态 PNG。示例曲库、原创封面与歌词只用于预览，不含歌曲文件；预览不是 Switch 截图。

NRO 结构、中文标题、版本、图标和匹配 ELF 的验证记录见 `build/diagnostics/release-0.5.1.json`；现有 `tools/inspect_nro.py` 的 16 语言栏位非空假设不适用于本机 nacptool，使用独立结构校验。

`tools/package_release.py` 仍用于 0.2.0 的归档，不能用于打包当前测试版；本版匹配 NRO／ELF／NACP 保存于 `build/diagnostics/release-0.5.1/`。

## 工程结构

`core`：扫描/队列/配置/重采样；`media`：标签、歌词、歌单与状态；`stream`：本地/HTTP 数据源；`decoder`：流式 MP3；`engine`、`audio`、`pause_gate`：播放状态机与音频输出；`assets`、`runtime`：缓存、封面和异步曲库；`app_model`：曲库分类与队列；`ui`、`ui_views.inc`：同源绘制与触摸命中；`main`：输入与应用生命周期。

旧 0.1.0 应用实现已移除，完整旧工程归档保存在本机工作备份中。新版设计与计划见仓库 `docs/superpowers/`。
