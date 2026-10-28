# DBI 921 简体中文

Switch 上的安装工具 DBI，配作者自己的多语言补丁（DBIPatcher），不是第三方汉化。

| 文件 | 版本 | 大小 | SHA256 |
|---|---|---|---|
| `DBI.nro` | 921（2026-10-23） | 16,637,485 B | `e76865ce5f006653f10d76a2f928a353486a7bed4f6d7acef69f4b44f7eb638f` |
| `translation_zhcn.bin` | 921 配套 | 787,364 B | `8aea07bcf2944bcda01d3f7d517d23d345900cf85e50b9b3b40f8adc31e6172c` |

上游：[rashevskyv/DBIPatcher](https://github.com/rashevskyv/DBIPatcher) 的 `921` release。
**注意：nro 和翻译文件必须是同一个 release，版本错配会显示乱码或者直接不生效。**

## 装到 SD 卡

`switch/DBI/` 目录下放两个文件，翻译文件要改名：

```
SD:/switch/DBI/DBI.nro
SD:/switch/DBI/translation.bin      ← translation_zhcn.bin 改名
```

没有 `switch/DBI/` 目录就自己建一个。

## 换回 912（HATS 自带的那版）

HATS-2026-10-16 里自带的是 912 版 + 非中文的 `translation.bin`。要回去的话，用 HATS 原包里的 `switch/DBI/` 覆盖，或者用本地留的备份：

```
~/Downloads/DBI-912-原版备份/DBI.nro.912
~/Downloads/DBI-912-原版备份/translation.bin.912
```

## 已知问题

- DBI 启动时会提示 `文件 /switch/prod.keys 缺少必需的密钥`，装游戏会失败。这不是 DBI 的问题，是本机还没转储密钥：Hekate → Payloads → `bootloader/payloads/Lockpick_RCM_Pro.bin` → 从 SysNAND 转储 → 生成 `sd:/switch/prod.keys`。
- `prod.keys` 是这台机器唯一的密钥，**不要提交、不要外传**。
