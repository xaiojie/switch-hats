# archive/ — 已被取代的东西

留着是为了考古和「当时为什么那么做」，**新工作不要用这里的代码**。

## game-library/ — 第一代局域网游戏库（DBI 边下边装）

- 时间：2026-10-27 ~ 10-28，已停止维护。
- 做法：Mac 上跑一个 Python 网页服务（`server.py`，约 6 万字节），把目录挂到 `http://<Mac>:8080/files/`，Switch 上用 **DBI** 配一条 `ApacheHTTP` 源，边下边装；第二个标签页用 DBI 的 FTP（5000）管 SD 卡。
- 为什么被取代：DBI 的 HTTP 安装要人手动在 DBI 里点，MTP/FTP 与安装互相抢；网页端拿不到真实安装进度和结果。`apps/switch-link/` 把「发现主机 → 推任务 → 主机自己 HTTP 拉流 → 装完回执」整条链路都做进了自己的 NRO，不再依赖 DBI，所以这里冻结。
- 仍然好用的部分：`server.py` 的静态文件服务带 Range，适合临时当文件服务器。

里面有 `游戏库/` 时不会入库（见根目录 `.gitignore`）。
