#!/bin/bash

echo "=========================================="
echo "UDP丢包诊断工具"
echo "=========================================="
echo ""

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}1. 检查UDP buffer大小${NC}"
echo "-------------------------------------------"
echo "当前系统UDP接收缓冲区："
sysctl net.core.rmem_max
sysctl net.core.rmem_default
echo ""
echo "当前系统UDP发送缓冲区："
sysctl net.core.wmem_max
sysctl net.core.wmem_default
echo ""

echo -e "${YELLOW}2. 检查网络统计（查看丢包）${NC}"
echo "-------------------------------------------"
echo "UDP统计信息："
netstat -su | grep -A 10 "Udp:"
echo ""

echo -e "${YELLOW}3. 检查comm_bridge进程的socket buffer${NC}"
echo "-------------------------------------------"
COMM_PID=$(pgrep -f comm_bridge_node)
if [ -z "$COMM_PID" ]; then
    echo -e "${RED}错误: comm_bridge_node 未运行！${NC}"
else
    echo "comm_bridge_node PID: $COMM_PID"
    echo "Socket信息："
    ss -unp | grep "$COMM_PID" | grep 8081
fi
echo ""

echo -e "${YELLOW}4. 测试话题频率（10秒）${NC}"
echo "-------------------------------------------"
echo "本地话题频率："
timeout 10s ros2 topic hz /uav1/fmu/out/vehicle_local_position 2>/dev/null | head -2 || echo "话题不存在"
echo ""

echo -e "${YELLOW}5. 网络质量测试${NC}"
echo "-------------------------------------------"
echo "请在其他机器上运行此命令来测试网络质量："
echo "  ping -c 100 <本机IP>"
echo ""

echo -e "${YELLOW}6. 推荐的优化措施${NC}"
echo "-------------------------------------------"
echo "如果丢包严重，尝试以下命令："
echo ""
echo "# 临时增大系统UDP缓冲区（需要sudo权限）："
echo "  sudo sysctl -w net.core.rmem_max=16777216"
echo "  sudo sysctl -w net.core.wmem_max=16777216"
echo ""
echo "# 降低广播频率："
echo "  broadcast_freq:=20.0  # 从50Hz降到20Hz"
echo ""
echo "# 检查网络带宽占用："
echo "  sudo iftop -i eth0 -f 'udp port 8081'"
echo ""
echo "=========================================="
