# docs/ — 文档索引

先看根目录 [README](../README.md) 的项目地图，再看这里。

## 面向「接手的人 / 其他 agent」

| 文档 | 内容 |
|---|---|
| [switch-hardware.md](switch-hardware.md) | **这台 Switch 的全部真实参数**：机型、固件、emuMMC、sysmodule、overlay、目录树、IP、FTP/HTTP 地址、已知的坑 |
| [device-access.md](device-access.md) | 怎么连上机器：地址发现、FTP/HTTP/UDP 用法、`Errno 61 vs 65` 怎么判断 |
| [dev-environment.md](dev-environment.md) | 怎么在本机编 NRO（devkitPro 装在 `~/devkitpro`，不用 Docker）、踩过的坑、构建产物校验 |
| [agent-prompts/ftp-and-device.md](agent-prompts/ftp-and-device.md) | 写给别的 agent 的一次性提示词（存档，判断方法通用） |

## 设计与计划（历史）

`superpowers/plans/`、`superpowers/specs/` 是 papa-walkman 0.2 → 0.3 重建时的设计与实施计划，保留当时的路径写法（`papa-walkman/`、`switch-link/`），对应现在仓库里的 `apps/papa-walkman/`、`apps/switch-link/`。

## 各子项目的文档

| 位置 | 内容 |
|---|---|
| [../cfw/README.md](../cfw/README.md) | 大气层整合包改了什么、怎么还原、校验值 |
| [../apps/switch-link/README.md](../apps/switch-link/README.md) | Switch Link 用法（HTTP 安装 + FTP 管理） |
| [../apps/switch-link/docs/FEATURES-AND-ROADMAP.md](../apps/switch-link/docs/FEATURES-AND-ROADMAP.md) | 主机端功能盘点 + P0/P1/P2 扩展路线 |
| [../apps/switch-link/console/README.md](../apps/switch-link/console/README.md) | 主机端源码结构与构建 |
| [../apps/switch-link/console/VALIDATION.md](../apps/switch-link/console/VALIDATION.md) | 实机验证记录 |
| [../apps/papa-walkman/README.md](../apps/papa-walkman/README.md) | 随身听用法、键位、配置 |
| [../apps/papa-walkman/VALIDATION.md](../apps/papa-walkman/VALIDATION.md) | 随身听验证记录 |
| [../research/dst-mods/STATUS-2026-10-29.md](../research/dst-mods/STATUS-2026-10-29.md) | 饥荒 mod 十一轮实验汇总（**已知未解决**） |
