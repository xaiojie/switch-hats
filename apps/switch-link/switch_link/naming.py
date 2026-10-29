"""SD 卡文件名的落盘编码。

这台 Switch（HOS 22.5.0 + emuMMC + Atmosphere 1.12.1）的 fs 服务处理不了
非 ASCII 文件名，实测三件事：

1. 一部分汉字直接建不出来：`fsFsCreateFile("sdmc:/music/文.txt")` 返回
   `0x202`（ftpsrv 里记作 FsError_PathNotFound），`ß.txt`、`Ж.txt`、`あ.txt`
   同样失败；而 `中.txt`、`café.txt` 能建出来。
2. 能建出来的名字在目录枚举里查不到：`LIST` / `MLSD` 都不返回它们，但用同样
   的名字 `SIZE` 又能拿到正确字节数（说明文件确实存在，只是看不见）。
3. 于是「上传中文名文件」在用户眼里就是：要么报 550 失败，要么传完了列表里没有。

绕开的办法是落盘只用 ASCII：把 UTF-8 字节做百分号编码，例如
`周杰伦 - 七里香.mp3` → `%E5%91%A8%E6%9D%B0%E4%BC%A6 - %E4%B8%83%E9%87%8C%E9%A6%99.mp3`。
纯 ASCII 名字（含空格、点、横线）原样保留；读取目录时再解码回中文给界面显示。
编码是可逆的、任何工具都能还原，也不需要额外的索引文件。
"""
from urllib.parse import unquote

MAX_STORAGE_NAME = 200


def is_ascii(name):
    return name.isascii()


def encode_component(name):
    """把一个路径片段变成能安全写到卡上的 ASCII 名字。"""
    if not name or name.isascii():
        return name
    out = []
    for byte in name.encode('utf-8'):
        if 0x20 <= byte <= 0x7E and byte != 0x25:  # 可打印 ASCII，'%' 除外
            out.append(chr(byte))
        else:
            out.append('%%%02X' % byte)
    return ''.join(out)


def decode_component(name):
    """把 encode_component 的结果还原成给人看的名字；不像编码的就原样返回。"""
    if not name or '%' not in name:
        return name
    try:
        decoded = unquote(name, encoding='utf-8', errors='strict')
    except (UnicodeDecodeError, ValueError):
        return name
    if decoded == name or encode_component(decoded) != name:
        return name
    return decoded


def encode_path(path):
    return '/'.join(encode_component(part) for part in path.split('/'))


def decode_path(path):
    return '/'.join(decode_component(part) for part in path.split('/'))


class NameTooLong(ValueError):
    pass


def storage_component(name):
    """落盘用的名字，附带长度保护（FAT 单个名字上限 255 字节）。"""
    encoded = encode_component(name)
    if len(encoded) > MAX_STORAGE_NAME:
        raise NameTooLong(
            '这个名字编码后太长（%d 字符），中文名会按每字 3 字节存成 ASCII 形式。请把名字改短一些。' % len(encoded))
    return encoded
