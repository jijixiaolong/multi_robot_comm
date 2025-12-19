# UDP丢包问题解决方案（100Hz → 70Hz，丢包30%）

## 🔴 问题分析

**症状**: 发送端100Hz，接收端只有70Hz，丢包率30%

**根本原因**: UDP接收缓冲区太小，无法处理100Hz的高频数据流

---

## ✅ 解决方案（按优先级）

### 🥇 方案1：增大系统UDP缓冲区限制（必须做！）

#### 步骤1：检查当前限制
```bash
sysctl net.core.rmem_max
sysctl net.core.wmem_max

# 如果输出小于8MB（8388608），则需要增大
```

#### 步骤2：临时增大（立即生效）
```bash
sudo sysctl -w net.core.rmem_max=16777216
sudo sysctl -w net.core.wmem_max=16777216
sudo sysctl -w net.core.rmem_default=8388608
sudo sysctl -w net.core.wmem_default=8388608
```

#### 步骤3：永久设置
```bash
sudo nano /etc/sysctl.conf

# 添加以下内容：
net.core.rmem_max = 16777216
net.core.wmem_max = 16777216  
net.core.rmem_default = 8388608
net.core.wmem_default = 8388608

# 保存后应用
sudo sysctl -p
```

#### 步骤4：重启comm_bridge
```bash
# 在每台电脑上重启节点
Ctrl+C  # 停止当前节点
ros2 launch multi_robot_comm comm_bridge.launch.py robot_id:=1 ...
```

---

### 🥈 方案2：降低发送频率（临时方案）

如果无法增大系统buffer，可以降低频率：

```bash
# 从100Hz降到50Hz
ros2 launch multi_robot_comm comm_bridge.launch.py \
    robot_id:=1 \
    uav_name:=uav1 \
    broadcast_ip:=192.168.1.255 \
    broadcast_freq:=50.0  # ← 降低频率
```

**效果**: 丢包率应该降到5%以下

---

### 🥉 方案3：检查网络质量

```bash
# 在接收端测试网络丢包
ping -c 100 -i 0.01 <发送端IP>

# 如果网络本身丢包>0.1%，需要检查：
# - 网线质量
# - 交换机性能
# - WiFi信号强度（如果用WiFi）
```

---

## 🔍 验证修复效果

### 1. 重新编译（已改进buffer配置）
```bash
cd ~/fastlab_study/multi_robot_comm_ws
colcon build --packages-select multi_robot_comm
source install/setup.bash
```

### 2. 启动comm_bridge，查看日志
```bash
ros2 launch multi_robot_comm comm_bridge.launch.py robot_id:=1 ...
```

**查找以下日志：**
```
[INFO] UDP recv buffer: requested=8192 KB, actual=8192 KB  ← 应该接近请求值
```

如果看到：
```
[WARN] ⚠️ CRITICAL: Recv buffer too small! Will cause 30% packet loss at 100Hz!
[WARN] ⚠️ Solution: sudo sysctl -w net.core.rmem_max=16777216
```

说明**系统限制太小**，必须先执行方案1！

### 3. 测试丢包率
```bash
# 发送端
ros2 topic hz /uav1/fmu/out/vehicle_local_position

# 接收端
ros2 topic hz /uav1/fmu/out/vehicle_local_position

# 期望：两者频率接近（差异<5%）
```

---

## 📊 预期结果

| 配置 | 发送频率 | 接收频率 | 丢包率 |
|------|----------|----------|--------|
| **修复前** | 100 Hz | 70 Hz | ❌ 30% |
| **修复后** | 100 Hz | 95-98 Hz | ✅ 2-5% |

---

## 🐛 如果仍然丢包

### 检查清单：

1. **系统buffer是否真的生效？**
   ```bash
   sysctl net.core.rmem_max  # 应该是 16777216
   ```

2. **comm_bridge日志中buffer大小是否符合预期？**
   ```
   [INFO] UDP recv buffer: requested=8192 KB, actual=8192 KB
   ```
   如果actual远小于requested，说明系统限制没生效。

3. **网络本身是否丢包？**
   ```bash
   ping -c 1000 -i 0.01 <对方IP>
   # 丢包应该 <0.1%
   ```

4. **CPU是否过载？**
   ```bash
   top
   # comm_bridge_node的CPU使用率应该 <20%
   ```

5. **三台电脑同时发送是否超出带宽？**
   ```bash
   # 每个UAV带宽：100Hz × 56bytes × 2(pos+att) = 11.2 KB/s
   # 三个UAV:  33.6 KB/s （非常小，应该不是问题）
   ```

---

## 🚀 快速修复命令（复制粘贴）

在**每台电脑**上执行：

```bash
# 1. 增大系统buffer
sudo sysctl -w net.core.rmem_max=16777216
sudo sysctl -w net.core.wmem_max=16777216

# 2. 重新编译
cd ~/fastlab_study/multi_robot_comm_ws
colcon build --packages-select multi_robot_comm
source install/setup.bash

# 3. 重启comm_bridge节点
```

完成！丢包率应该降到5%以下。
