#!/bin/bash
# 双击启动「爬爬的 Switch 游戏库」
cd "$(dirname "$0")" || exit 1

PORT="${1:-8080}"

# 已经在跑就只是打开浏览器
if curl -s --max-time 1 "http://127.0.0.1:${PORT}/api/status" 2>/dev/null | grep -q '"app"'; then
  echo "🐋 游戏库已经在 ${PORT} 端口跑着了，帮你打开浏览器。"
  open "http://127.0.0.1:${PORT}/"
  sleep 1
  exit 0
fi

echo "🐋 正在启动爬爬的 Switch 游戏库…"
/usr/bin/python3 server.py --port "$PORT"
echo
read -n 1 -s -r -p "服务已停止，按任意键关闭窗口…"
