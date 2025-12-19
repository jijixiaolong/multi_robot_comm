# 三机真实部署测试指南（三台电脑+三个飞控）

## 📋 硬件配置

- **电脑A**: 连接飞控1（UAV1）
- **电脑B**: 连接飞控2（UAV2）
- **电脑C**: 连接飞控3（UAV3）
- **网络**: 三台电脑在同一局域网（WiFi或有线）

---

## 🌐 第一步：网络配置

### 1. 确认三台电脑在同一网段

在**每台电脑**上检查IP：

```bash
ip addr show
# 或
ifconfig
```

#### 示例配置（假设使用有线网络 eth0）：

| 电脑 | IP地址 | 网段 | 广播地址 |
|------|--------|------|----------|
| 电脑A | 192.168.1.101 | 192.168.1.0/24 | 192.168.1.255 |
| 电脑B | 192.168.1.102 | 192.168.1.0/24 | 192.168.1.255 |
| 电脑C | 192.168.1.103 | 192.168.1.0/24 | 192.168.1.255 |

> **重要**: 确保广播地址一致！例如 `192.168.1.255`

### 2. 配置静态IP（推荐）

如果使用DHCP可能获得不同网段的IP，建议配置静态IP：

**临时设置（重启失效）：**
```bash
# 电脑A
sudo ip addr add 192.168.1.101/24 dev eth0

# 电脑B
sudo ip addr add 192.168.1.102/24 dev eth0

# 电脑C
sudo ip addr add 192.168.1.103/24 dev eth0
```

**永久设置（Ubuntu 20.04+）：**
编辑 `/etc/netplan/01-netcfg.yaml`：
```yaml
network:
  version: 2
  ethernets:
    eth0:  # 改成你的网卡名称
      addresses:
        - 192.168.1.101/24  # 每台电脑不同
      gateway4: 192.168.1.1
      nameservers:
        addresses: [8.8.8.8, 8.8.4.4]
```

应用配置：
```bash
sudo netplan apply
```

### 3. 测试网络连通性

在**电脑A**上：
```bash
ping 192.168.1.102  # ping 电脑B
ping 192.168.1.103  # ping 电脑C
```

在**电脑B**上：
```bash
ping 192.168.1.101  # ping 电脑A
ping 192.168.1.103  # ping 电脑C
```

✅ **确保所有电脑能互相ping通！**

### 4. 防火墙配置

在**每台电脑**上允许UDP 8081端口：

```bash
sudo ufw allow 8081/udp
sudo ufw status
```

---

## 🔧 第二步：在每台电脑上部署代码

### 电脑A配置（UAV1）

```bash
# 1. 编译工作空间
cd ~/fastlab_study/multi_robot_comm_ws
source /opt/ros/humble/setup.bash
source ~/ws_sensor_combined/install/setup.bash  # px4_msgs依赖
colcon build --packages-select multi_robot_comm
source install/setup.bash

# 2. 确认飞控连接
ros2 topic list | grep fmu
# 应该看到 /uav1/fmu/out/vehicle_local_position 等话题

# 3. 启动通信桥
ros2 launch multi_robot_comm comm_bridge.launch.py \
    robot_id:=1 \
    uav_name:=uav1 \
    broadcast_ip:=192.168.1.255 \
    broadcast_freq:=50.0
```

### 电脑B配置（UAV2）

```bash
# 1. 编译工作空间
cd ~/fastlab_study/multi_robot_comm_ws
source /opt/ros/humble/setup.bash
source ~/ws_sensor_combined/install/setup.bash
colcon build --packages-select multi_robot_comm
source install/setup.bash

# 2. 确认飞控连接
ros2 topic list | grep fmu

# 3. 启动通信桥
ros2 launch multi_robot_comm comm_bridge.launch.py \
    robot_id:=2 \
    uav_name:=uav2 \
    broadcast_ip:=192.168.1.255 \
    broadcast_freq:=50.0
```

### 电脑C配置（UAV3）

```bash
# 1. 编译工作空间
cd ~/fastlab_study/multi_robot_comm_ws
source /opt/ros/humble/setup.bash
source ~/ws_sensor_combined/install/setup.bash
colcon build --packages-select multi_robot_comm
source install/setup.bash

# 2. 确认飞控连接
ros2 topic list | grep fmu

# 3. 启动通信桥
ros2 launch multi_robot_comm comm_bridge.launch.py \
    robot_id:=3 \
    uav_name:=uav3 \
    broadcast_ip:=192.168.1.255 \
    broadcast_freq:=50.0
```

---

## ✅ 第三步：验证通信

### 在电脑A上验证

```bash
# 1. 查看所有话题（应该看到3个UAV的话题）
ros2 topic list | grep vehicle

# 期望输出：
# /uav1/fmu/out/vehicle_local_position  # 本地飞控
# /uav1/fmu/out/vehicle_attitude
# /uav2/fmu/out/vehicle_local_position  # 从电脑B接收
# /uav2/fmu/out/vehicle_attitude
# /uav3/fmu/out/vehicle_local_position  # 从电脑C接收
# /uav3/fmu/out/vehicle_attitude

# 2. 监控UAV2的位置话题频率
ros2 topic hz /uav2/fmu/out/vehicle_local_position

# 3. 监控UAV3的位置话题频率
ros2 topic hz /uav3/fmu/out/vehicle_local_position

# 4. 查看UAV2的实时数据
ros2 topic echo /uav2/fmu/out/vehicle_local_position
```

### 在电脑B上验证

```bash
ros2 topic list | grep vehicle
# 应该看到所有3个UAV的话题

ros2 topic hz /uav1/fmu/out/vehicle_local_position  # 从A接收
ros2 topic hz /uav3/fmu/out/vehicle_local_position  # 从C接收
```

### 在电脑C上验证

```bash
ros2 topic list | grep vehicle
# 应该看到所有3个UAV的话题

ros2 topic hz /uav1/fmu/out/vehicle_local_position  # 从A接收
ros2 topic hz /uav2/fmu/out/vehicle_local_position  # 从B接收
```

---

## 📊 预期结果

### ✅ 成功标志

1. **每台电脑都能看到6个话题**（3个position + 3个attitude）
2. **远程话题频率正常**（接近50Hz或飞控实际发布频率）
3. **comm_bridge日志显示**：
   ```
   [INFO] Created position publisher for UAV 2: /uav2/fmu/out/vehicle_local_position
   [INFO] Created position publisher for UAV 3: /uav3/fmu/out/vehicle_local_position
   ```
4. **可以在任意电脑上echo其他UAV的数据**

---

## 🐛 常见问题排查

### ❌ 问题1：看不到其他UAV的话题

**原因分析：**
- 网络不通
- 广播地址错误
- 防火墙阻止

**解决方案：**
```bash
# 1. 检查网络连通性
ping 192.168.1.102
ping 192.168.1.103

# 2. 检查广播地址
ip addr show | grep broadcast

# 3. 检查防火墙
sudo ufw status
sudo ufw allow 8081/udp

# 4. 使用tcpdump抓包验证UDP广播
sudo tcpdump -i any -n udp port 8081
# 应该看到来自其他IP的UDP包
```

### ❌ 问题2：话题频率很低（<10Hz）

**原因分析：**
- 飞控本身发布频率低
- 网络丢包严重

**解决方案：**
```bash
# 1. 检查本地飞控发布频率
ros2 topic hz /uav1/fmu/out/vehicle_local_position

# 2. 检查网络质量
ping 192.168.1.102 -c 100
# 查看丢包率

# 3. 降低broadcast_freq参数
ros2 launch multi_robot_comm comm_bridge.launch.py \
    robot_id:=1 \
    uav_name:=uav1 \
    broadcast_ip:=192.168.1.255 \
    broadcast_freq:=20.0  # 降低到20Hz
```

### ❌ 问题3：robot_id冲突

**现象：** comm_bridge日志显示收到自己的消息

**解决方案：**
确保每台电脑的 `robot_id` **唯一**：
- 电脑A: `robot_id:=1`
- 电脑B: `robot_id:=2`
- 电脑C: `robot_id:=3`

### ❌ 问题4：ROS_DOMAIN_ID不一致

**现象：** 三台电脑看到的话题数量不一样

**解决方案：**
确保三台电脑使用**相同的ROS_DOMAIN_ID**：
```bash
# 在每台电脑上
export ROS_DOMAIN_ID=0
```

或在 `~/.bashrc` 中永久设置：
```bash
echo 'export ROS_DOMAIN_ID=0' >> ~/.bashrc
source ~/.bashrc
```

---

## 🔍 高级诊断

### 1. 抓包分析UDP广播

在电脑A上：
```bash
sudo tcpdump -i any -n -X udp port 8081 | grep -A 20 "192.168.1"
```

应该看到：
- 从 `192.168.1.102` 发来的包（UAV2）
- 从 `192.168.1.103` 发来的包（UAV3）

### 2. 监控网络带宽

```bash
sudo apt install iftop
sudo iftop -i eth0 -f "udp port 8081"
```

### 3. 检查buffer size

确认 comm_bridge 日志中没有警告：
```
[WARN] Failed to set UDP send buffer size
[WARN] Failed to set UDP recv buffer size
```

如果有警告，尝试增大系统限制：
```bash
sudo sysctl -w net.core.rmem_max=8388608
sudo sysctl -w net.core.wmem_max=8388608
```

---

## 📝 完整测试清单

### 网络配置
- [ ] 三台电脑在同一网段（例如 192.168.1.x）
- [ ] 三台电脑能互相ping通
- [ ] 防火墙允许UDP 8081端口
- [ ] 广播地址正确（例如 192.168.1.255）

### 软件配置
- [ ] 三台电脑都编译了 multi_robot_comm
- [ ] 三台电脑的 ROS_DOMAIN_ID 一致
- [ ] 每台电脑的 robot_id 唯一（1, 2, 3）
- [ ] 每台电脑的 uav_name 唯一（uav1, uav2, uav3）
- [ ] 飞控已连接且发布PX4消息

### 通信验证
- [ ] 每台电脑能看到6个话题
- [ ] 远程话题频率正常（>20Hz）
- [ ] 可以echo其他UAV的数据
- [ ] comm_bridge日志正常，无错误

---

## 🚀 一键启动脚本

创建 `start_uav1.sh` 在电脑A上：
```bash
#!/bin/bash
export ROS_DOMAIN_ID=0
cd ~/fastlab_study/multi_robot_comm_ws
source install/setup.bash
ros2 launch multi_robot_comm comm_bridge.launch.py \
    robot_id:=1 \
    uav_name:=uav1 \
    broadcast_ip:=192.168.1.255 \
    broadcast_freq:=50.0
```

创建 `start_uav2.sh` 在电脑B上：
```bash
#!/bin/bash
export ROS_DOMAIN_ID=0
cd ~/fastlab_study/multi_robot_comm_ws
source install/setup.bash
ros2 launch multi_robot_comm comm_bridge.launch.py \
    robot_id:=2 \
    uav_name:=uav2 \
    broadcast_ip:=192.168.1.255 \
    broadcast_freq:=50.0
```

创建 `start_uav3.sh` 在电脑C上：
```bash
#!/bin/bash
export ROS_DOMAIN_ID=0
cd ~/fastlab_study/multi_robot_comm_ws
source install/setup.bash
ros2 launch multi_robot_comm comm_bridge.launch.py \
    robot_id:=3 \
    uav_name:=uav3 \
    broadcast_ip:=192.168.1.255 \
    broadcast_freq:=50.0
```

给脚本添加执行权限：
```bash
chmod +x start_uav*.sh
```

---

## 📸 成功截图示例

在电脑A上运行：
```bash
ros2 topic list | grep vehicle
```

应该看到：
```
/uav1/fmu/out/vehicle_attitude
/uav1/fmu/out/vehicle_local_position
/uav2/fmu/out/vehicle_attitude       ← 从电脑B接收
/uav2/fmu/out/vehicle_local_position ← 从电脑B接收
/uav3/fmu/out/vehicle_attitude       ← 从电脑C接收
/uav3/fmu/out/vehicle_local_position ← 从电脑C接收
```

**祝部署成功！** 🎉
