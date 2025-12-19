# Multi-Robot Communication Package (multi_robot_comm)

基于 [ZJU FAST Lab](https://github.com/ZJU-FAST-Lab/ego-planner-swarm) 的 `rosmsg_tcp_bridge` 模块开发的 **PX4 多机通信功能包**。

## 特性

- ✅ **UDP 广播通信**：低延迟、两两互通
- ✅ **PX4 原生支持**：`VehicleLocalPosition` 和 `VehicleAttitude`
- ✅ **动态话题发布**：自动为每个远程 UAV 创建独立话题（`/uavX/fmu/out/...`）
- ✅ **去中心化设计**：无需中央服务器
- ✅ **支持 N 台 UAV**：自动扩展

## 支持的消息类型

| 消息类型 | 本地话题 | 远程话题（动态） |
|---------|---------|----------------|
| `VehicleLocalPosition` | `/uav1/fmu/out/vehicle_local_position` | `/uav2/fmu/out/vehicle_local_position` |
| `VehicleAttitude` | `/uav1/fmu/out/vehicle_attitude` | `/uav2/fmu/out/vehicle_attitude` |

> **注意**：远程话题会根据接收到的消息自动创建，如 `/uav2/`, `/uav3/` 等。

## 快速开始

### 1. 编译

```bash
cd ~/fastlab_study/multi_robot_comm_ws
source /opt/ros/humble/setup.bash
source ~/ws_sensor_combined/install/setup.bash  # px4_msgs
colcon build --packages-select multi_robot_comm
source install/setup.bash
```

### 2. 三台 UAV 部署示例

**UAV1 (IP: 10.220.45.176):**
```bash
ros2 launch multi_robot_comm comm_bridge.launch.py \
    robot_id:=1 \
    uav_name:=uav_1 \
    broadcast_ip:=10.220.45.255
```

**UAV2 (IP: 10.220.45.177):**
```bash
ros2 launch multi_robot_comm comm_bridge.launch.py \
    robot_id:=2 \
    uav_name:=uav_2 \
    broadcast_ip:=10.220.45.255
```

**UAV3 (IP: 10.220.45.178):**
```bash
ros2 launch multi_robot_comm comm_bridge.launch.py \
    robot_id:=3 \
    uav_name:=uav_3 \
    broadcast_ip:=10.220.45.255
```

### 3. 验证通信

在任意 UAV 上查看话题：
```bash
ros2 topic list | grep fmu

# 应该看到：
# /uav_1/fmu/out/vehicle_local_position
# /uav_1/fmu/out/vehicle_attitude
# /uav_2/fmu/out/vehicle_local_position  # 自动创建
# /uav_2/fmu/out/vehicle_attitude        # 自动创建
# /uav_3/fmu/out/vehicle_local_position  # 自动创建
# /uav_3/fmu/out/vehicle_attitude        # 自动创建
```

监听远程 UAV 数据：
```bash
# 在 UAV1 上监听 UAV2 的位置
ros2 topic echo /uav_2/fmu/out/vehicle_local_position
```

## 参数说明

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `robot_id` | 1 | 本机 UAV 唯一 ID（1, 2, 3, ...） |
| `uav_name` | uav_1 | 本机 UAV 名称（uav_1, uav_2, uav_3, ...）支持下划线 |
| `broadcast_ip` | 127.0.0.255 | UDP 广播地址（如 `10.220.45.255`） |
| `broadcast_freq` | 50.0 | 广播频率 (Hz) |
| `position_topic_suffix` | /fmu/out/vehicle_local_position | 位置话题后缀 |
| `attitude_topic_suffix` | /fmu/out/vehicle_attitude | 姿态话题后缀 |

## 网络配置

### 广播地址获取

```bash
# 查看网络配置
ip addr show

# 或
ifconfig

# 例如：
# inet 10.220.45.176/24  =>  广播地址: 10.220.45.255
```

### 防火墙配置

```bash
# 允许 UDP 8081 端口
sudo ufw allow 8081/udp
sudo ufw status
```

## 工作原理

### 通信流程

1. **发送端**: 订阅 `/uav1/fmu/out/vehicle_local_position` → 序列化 → UDP 广播
2. **接收端**: 接收 UDP 广播 → 反序列化 → 动态创建 `/uav1/fmu/out/vehicle_local_position` → 发布
3. **回环过滤**: 通过 `robot_id` 过滤自己的消息

### 动态发布者

系统会自动为每个远程 UAV 创建独立的发布者：
- 收到 `robot_id=1` 的消息 → 创建 `/uav1/fmu/out/vehicle_local_position`
- 收到 `robot_id=2` 的消息 → 创建 `/uav2/fmu/out/vehicle_local_position`
- 收到 `robot_id=3` 的消息 → 创建 `/uav3/fmu/out/vehicle_local_position`

### UDP vs TCP

| 特性 | UDP（当前实现） | TCP（ego-planner） |
|------|----------------|-------------------|
| 延迟 | ✅ 最低（一次广播） | ⚠️ 中等（环形中转） |
| 可靠性 | ⚠️ 可能丢包 | ✅ 保证送达 |
| 复杂度 | ✅ 简单 | ⚠️ 复杂 |
| 适用场景 | 高频状态（50Hz） | 关键数据（轨迹） |

**为什么选择 UDP？**
- PX4 位置/姿态是 50Hz 高频数据
- 偶尔丢 1-2 帧影响很小
- 延迟最低（关键！）

## 故障排查

### 收不到远程消息

1. **检查网络**
   ```bash
   ping <远程 UAV IP>
   ```

2. **检查广播地址**
   - ❌ 不要用 `127.0.0.255`（仅本机）
   - ✅ 使用真实网络广播地址（如 `10.220.45.255`）

3. **检查 robot_id**
   - 确保每台 UAV 的 `robot_id` 唯一

4. **检查防火墙**
   ```bash
   sudo ufw status
   # 确保 8081/udp 已允许
   ```

### 话题未自动创建

- 等待几秒（话题按需创建）
- 确认远程 UAV 正在发布 PX4 消息
- 查看日志：寻找 "Created position publisher for UAV X"

## 测试

详细测试指南请参考项目根目录的 `TESTING.md`。

## 与 ego-planner-swarm 的区别

| 特性 | ego-planner-swarm | multi_robot_comm |
|------|------------------|------------------|
| 通信方式 | TCP 环形 + UDP 广播 | 纯 UDP 广播 |
| 消息类型 | Odometry + 轨迹 | PX4 Position + Attitude |
| 话题命名 | 通用话题 | 动态创建（按 UAV ID） |
| 依赖 | Boost | 标准 C++17 |
| 复杂度 | 高 | 低 |

## License

MIT License
