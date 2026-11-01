# 0.4.0 本地交付与验证

日期：2026-10-30。用户明确要求先完成代码和编译，Switch 暂不在身边。本版未部署、未做真机验收；卡上仍应为之前已验证的 0.3.12。

## 本版完成

- 原生 1280×720 暖色界面，曲库／正在播放／设置三个主入口；歌词和队列归属播放页。主入口 58 px 高，主要按钮 64–88 px，歌曲行距 72 px。
- 播放页默认大封面，轻触封面切换磁带；显示曲名、歌手、专辑、当前／下一句歌词。底部统一大播放、上首和下首按钮。
- 曲库切换 SD／云端／收藏／歌单，按歌曲／专辑／歌手浏览。队列先选中再执行播放、排序或移除。
- 手指拖动实时移动列表；进度拖动时预览，松手只提交一次跳转。跨页面或切歌时取消旧进度手势，避免误跳到新歌曲中间。
- A 在播放／歌词页只控制播放暂停，X 打开队列、Y 打开歌词、B 返回播放页，− 循环三个主入口。歌单内 Y 收藏歌曲；只有歌单总览 Y 删除歌单，与提示保持一致。
- 输入轮询目标 60 Hz，按需重绘，静态页面 10 Hz 更新；缓存字形、缩放图像、分组索引和普通歌词换行。实际 Switch 帧率和延迟尚待设备验证。
- 保存进度、设置、收藏和缓存维护改为后台任务；日志批量异步写入。保存失败可重试，周期进度保存不会覆盖待保存的完整收藏／歌单状态；退出时等待最终保存和线程回收。

## 封面／歌词与旧来源兼容

原有云端 M3U 缺少完整歌词地址，旧客户端又跳过云端标签提取。服务器现在提供 JSON 字段，客户端优先使用其中的 cover／lyrics／albumArtist／duration 等信息。对于原配置的 `https://kiro.papaclaoud.top/library.m3u`，请求优先使用同服务器 JSON；失败时退回原 M3U。配置来源 ID 和离线索引键保持原值。

实际 JSON 和 M3U 的 90 首可播放歌曲按音频 URL 逐一核对 ID 相同，旧收藏对应关系保留。已过滤显式 switchPlayable=false、非 MP3 format 和已知不支持的扩展名；支持无扩展名的 MP3 流。

完整目录信息不再额外请求音频首部。缺失信息时按需读取有上限的 ID3 前缀，兼容 UTF-8／GBK 中文标签；封面先发布，歌词继续后台读取。没有歌词地址时可使用精确歌曲信息匹配的公开歌词服务，结果受时长／专辑／歌手校验与缓存约束。曲目 ID、音频路径和请求代数共同阻止过期结果覆盖新歌曲。

2026-10-30 实际曲库：91 条，其中 90 首 MP3 和 1 首 FLAC；可播放的 90 首中，86 首有封面地址、88 首有歌词地址。原生 C++ 客户端抽查《菊花台》：封面解码成功、24 行同步歌词、时长 293.973 秒。检查仅记录数量，不复制歌曲歌词内容。

## 验证结果

- ASan／UBSan Debug 宿主构建通过，CTest **13/13**：core、path、decoder、engine、media、app_model、runtime、pause_gate、ui、interaction、stream_no_detach、assets、audio。
- 真实 HTTP 测试覆盖重定向、Range／不支持 Range、截断、取消、时长／跳转、缓存上限、歌词精确匹配、过期请求、渐进封面发布，并注入 pthread_detach 返回 ENOSYS，保留 0.3.12 启动修复。
- 排除一次间歇失败的原因：HTTP 写回调可越过预取窗口，使测试有时直接读到已缓存内容而不发起预期的 Range 请求。新增确定性窗口边界测试先失败，再限制每次写入的剩余预取字节。修复后完整测试通过，额外重复结果见 `build/diagnostics/stream-repeat-0.4.0.txt`。
- 新增后台存储失败重试测试暴露来源保存失败后未重试的问题；修复后最新来源与用户状态均可恢复保存。
- 独立只读代码复核发现歌单内 Y 误删整张歌单，已通过提取统一快捷键策略修正并覆盖回归测试；复核还指出旧 seek 手势跨切歌，已加歌曲身份校验和先失败后通过的测试。未遗留复核提出的代码问题。
- 原生渲染器生成 14 张页面／状态预览并检查曲库、专辑、播放、歌词、设置的布局；演示数据不是设备截图。ASan Debug 的宿主绘制数据只用于本地比较，不代表 Switch 性能。
- `bash build.sh` 交叉编译通过。NRO 节区和资源边界校验通过，NACP 与此前基线逐字节比较，仅版本字段不同，图标相同。

证据在 `build/diagnostics/ctest-0.4.0.txt`、`cloud-live-0.4.0.txt`、`catalog-0.4.0.json`、`build-0.4.0.txt`、`release-0.4.0.json` 和 `ui-0.4.0/`。完整复核与处理记录在 `review-0.4.0.md`。

## 文件与产物

- 界面：`source/ui.hpp`、`source/ui.cpp`、`source/ui_views.inc`、`tools/preview.cpp`。
- 操作与主循环：`source/main.cpp`、`source/interaction.hpp/.cpp`、`source/app_model.hpp/.cpp`。
- 媒体与网络：`source/media.hpp/.cpp`、`source/assets.hpp/.cpp`、`source/stream.hpp/.cpp`。
- 后台维护与日志：`source/runtime.hpp/.cpp`、`source/log.hpp/.cpp`；继续使用已有 `fslock`，未引入 detach。
- 版本与构建：`CMakeLists.txt` 两处为 0.4.0，`source/log.cpp` 的 BuildVersionMinor=0、BuildVersionSeries=4。
- 对应回归测试：`tests/{interaction,app_model,ui,media,runtime,stream}_tests.cpp`、`tests/stream_server.py`、`tests/run_stream_tests.py`。

构建产物 `build/papa-walkman.nro`：2,008,357 字节。

SHA256：`5d6ebb9f06ed0f0f15fdf70239c6146e4cd02338f478e2736d36d33b50ad496e`。

匹配的 NRO／ELF／NACP 另存于 `build/diagnostics/release-0.4.0/`，便于后续崩溃反查。

## 待真机验证

保持原安装、forwarder 和标题 ID，后续只原位覆盖 `/sdmc:/switch/papa-walkman/papa-walkman.nro`。推送后完整回读核对上述 SHA256，清空并回读确认 log／trace 为零字节。

从原桌面图标启动，检查三个主入口、切歌、播放暂停、列表拖动、队列与歌单操作、实际封面／歌词及持续播放。退出后取回日志，核对最终保存、线程回收、绘制统计、崩溃报告。此前用户曾报告来源未确认的启动弹窗；本次无设备，不能声称该弹窗消失或新版已达到真机 60 FPS。
