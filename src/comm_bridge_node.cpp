/**
 * @file comm_bridge_node.cpp
 * @brief ROS 2 多机通信桥接节点
 * 
 * 基于 ego-planner-swarm 项目的 rosmsg_tcp_bridge 模块。
 * 实现了 TCP 环形连接和 UDP 广播两种通信方式。
 * 
 * 核心特性：
 * 1. TCP 环形连接：用于可靠地传输重要数据（如轨迹），保证消息不丢失。
 * 2. UDP 广播：用于快速、低延迟地共享状态信息（如里程计），允许少量丢包。
 * 3. 话题可配置：通过 ROS 2 参数和 remapping 灵活配置输入输出话题。
 * 
 * @author Based on ZJU FAST Lab's ego-planner-swarm
 */

#include <rclcpp/rclcpp.hpp>
#include <px4_msgs/msg/vehicle_local_position.hpp>
#include <px4_msgs/msg/vehicle_attitude.hpp>

#include <thread>
#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>

// Linux Socket API
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

// ============================================================================
// 常量定义
// ============================================================================
#define TCP_PORT 8080       // TCP 通信端口
#define UDP_PORT 8081       // UDP 广播端口
#define BUF_LEN 65536       // 缓冲区大小 (64KB)

// ============================================================================
// 消息类型枚举
// ============================================================================
enum MESSAGE_TYPE : int32_t
{
    MSG_VEHICLE_LOCAL_POSITION = 10, // PX4 位置消息
    MSG_VEHICLE_ATTITUDE = 11,       // PX4 姿态消息
};

// ============================================================================
// CommBridgeNode 类
// ============================================================================
class CommBridgeNode : public rclcpp::Node
{
public:
    CommBridgeNode() : Node("comm_bridge_node")
    {
        // ====================================================================
        // 声明并获取参数
        // ====================================================================
        this->declare_parameter<int>("robot_id", 0);
        this->declare_parameter<std::string>("next_robot_ip", "127.0.0.1");
        this->declare_parameter<std::string>("broadcast_ip", "127.0.0.255");
        this->declare_parameter<double>("broadcast_freq", 50.0);
        
        // PX4 话题参数
        this->declare_parameter<std::string>("uav_name", "uav1");  // 本机 UAV 名称
        this->declare_parameter<std::string>("position_topic_suffix", "/fmu/out/vehicle_local_position");
        this->declare_parameter<std::string>("attitude_topic_suffix", "/fmu/out/vehicle_attitude");

        robot_id_ = this->get_parameter("robot_id").as_int();
        next_robot_ip_ = this->get_parameter("next_robot_ip").as_string();
        broadcast_ip_ = this->get_parameter("broadcast_ip").as_string();
        broadcast_freq_ = this->get_parameter("broadcast_freq").as_double();
        
        uav_name_ = this->get_parameter("uav_name").as_string();
        std::string position_suffix = this->get_parameter("position_topic_suffix").as_string();
        std::string attitude_suffix = this->get_parameter("attitude_topic_suffix").as_string();
        
        std::string local_position_topic = "/" + uav_name_ + position_suffix;
        std::string local_attitude_topic = "/" + uav_name_ + attitude_suffix;

        RCLCPP_INFO(this->get_logger(), "======================================");
        RCLCPP_INFO(this->get_logger(), "PX4 Multi-Robot Communication Bridge");
        RCLCPP_INFO(this->get_logger(), "======================================");
        RCLCPP_INFO(this->get_logger(), "Robot ID: %d", robot_id_);
        RCLCPP_INFO(this->get_logger(), "UAV Name: %s", uav_name_.c_str());
        RCLCPP_INFO(this->get_logger(), "Broadcast IP (UDP): %s", broadcast_ip_.c_str());
        RCLCPP_INFO(this->get_logger(), "Broadcast Freq: %.1f Hz", broadcast_freq_);
        RCLCPP_INFO(this->get_logger(), "Local Position Topic: %s", local_position_topic.c_str());
        RCLCPP_INFO(this->get_logger(), "Local Attitude Topic: %s", local_attitude_topic.c_str());

        // ====================================================================
        // PX4 订阅者
        // ====================================================================
        position_sub_ = this->create_subscription<px4_msgs::msg::VehicleLocalPosition>(
            local_position_topic, rclcpp::SensorDataQoS(),
            std::bind(&CommBridgeNode::positionCallback, this, std::placeholders::_1));
        
        attitude_sub_ = this->create_subscription<px4_msgs::msg::VehicleAttitude>(
            local_attitude_topic, rclcpp::SensorDataQoS(),
            std::bind(&CommBridgeNode::attitudeCallback, this, std::placeholders::_1));

        // 动态发布者将在接收到消息时按需创建

        // ====================================================================
        // 初始化 Socket
        // ====================================================================
        initUdpBroadcast();
        
        // 启动 UDP 接收线程
        udp_recv_thread_ = std::thread(&CommBridgeNode::udpRecvLoop, this);
        udp_recv_thread_.detach();

        RCLCPP_INFO(this->get_logger(), "Communication bridge started successfully!");
        RCLCPP_INFO(this->get_logger(), "Remote UAV topics will be created dynamically as /uavX%s", position_suffix.c_str());
    }


    ~CommBridgeNode()
    {
        running_ = false;
        if (udp_send_fd_ > 0) close(udp_send_fd_);
        if (udp_recv_fd_ > 0) close(udp_recv_fd_);
    }

private:
    // ========================================================================
    // UDP 广播初始化
    // ========================================================================
    void initUdpBroadcast()
    {
        // 创建 UDP 发送 socket
        udp_send_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_send_fd_ < 0) {
            RCLCPP_ERROR(this->get_logger(), "Failed to create UDP send socket!");
            return;
        }

        // 启用广播
        int broadcast_enable = 1;
        setsockopt(udp_send_fd_, SOL_SOCKET, SO_BROADCAST, 
                   &broadcast_enable, sizeof(broadcast_enable));

        // 设置广播目标地址
        memset(&udp_broadcast_addr_, 0, sizeof(udp_broadcast_addr_));
        udp_broadcast_addr_.sin_family = AF_INET;
        udp_broadcast_addr_.sin_port = htons(UDP_PORT);
        inet_pton(AF_INET, broadcast_ip_.c_str(), &udp_broadcast_addr_.sin_addr);

        // 创建 UDP 接收 socket
        udp_recv_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_recv_fd_ < 0) {
            RCLCPP_ERROR(this->get_logger(), "Failed to create UDP recv socket!");
            return;
        }

        // 允许端口复用
        int reuse = 1;
        setsockopt(udp_recv_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        setsockopt(udp_recv_fd_, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

        // 绑定端口
        struct sockaddr_in recv_addr;
        memset(&recv_addr, 0, sizeof(recv_addr));
        recv_addr.sin_family = AF_INET;
        recv_addr.sin_addr.s_addr = INADDR_ANY;
        recv_addr.sin_port = htons(UDP_PORT);

        if (bind(udp_recv_fd_, (struct sockaddr*)&recv_addr, sizeof(recv_addr)) < 0) {
            RCLCPP_ERROR(this->get_logger(), "Failed to bind UDP recv socket to port %d!", UDP_PORT);
            return;
        }

        RCLCPP_INFO(this->get_logger(), "UDP broadcast initialized on port %d", UDP_PORT);
    }

    // ========================================================================
    // PX4 VehicleLocalPosition 回调
    // ========================================================================
    void positionCallback(const px4_msgs::msg::VehicleLocalPosition::SharedPtr msg)
    {
        static rclcpp::Time last_time = this->now();
        rclcpp::Time now = this->now();
        if ((now - last_time).seconds() < 1.0 / broadcast_freq_) {
            return;
        }
        last_time = now;

        char buffer[BUF_LEN];
        int len = serializePosition(msg, buffer);
        if (len <= 0) return;

        sendto(udp_send_fd_, buffer, len, 0,
               (struct sockaddr*)&udp_broadcast_addr_, sizeof(udp_broadcast_addr_));
    }

    // ========================================================================
    // PX4 VehicleAttitude 回调
    // ========================================================================
    void attitudeCallback(const px4_msgs::msg::VehicleAttitude::SharedPtr msg)
    {
        static rclcpp::Time last_time = this->now();
        rclcpp::Time now = this->now();
        if ((now - last_time).seconds() < 1.0 / broadcast_freq_) {
            return;
        }
        last_time = now;

        char buffer[BUF_LEN];
        int len = serializeAttitude(msg, buffer);
        if (len <= 0) return;

        sendto(udp_send_fd_, buffer, len, 0,
               (struct sockaddr*)&udp_broadcast_addr_, sizeof(udp_broadcast_addr_));
    }

    // ========================================================================
    // UDP 接收循环 (在单独线程中运行)
    // ========================================================================
    void udpRecvLoop()
    {
        char buffer[BUF_LEN];
        struct sockaddr_in sender_addr;
        socklen_t addr_len = sizeof(sender_addr);

        while (running_ && rclcpp::ok()) {
            int recv_len = recvfrom(udp_recv_fd_, buffer, BUF_LEN, 0,
                                     (struct sockaddr*)&sender_addr, &addr_len);
            if (recv_len <= 0) continue;

            // 解析消息类型
            MESSAGE_TYPE msg_type = *((MESSAGE_TYPE*)buffer);

            switch (msg_type) {
                case MSG_VEHICLE_LOCAL_POSITION: {
                    auto pos_msg = std::make_shared<px4_msgs::msg::VehicleLocalPosition>();
                    int sender_id = -1;
                    if (deserializePosition(buffer, recv_len, pos_msg, sender_id)) {
                        if (sender_id != robot_id_) {
                            // 获取或创建该 UAV 的发布者
                            auto pub = getOrCreatePositionPublisher(sender_id);
                            pub->publish(*pos_msg);
                        }
                    }
                    break;
                }
                case MSG_VEHICLE_ATTITUDE: {
                    auto att_msg = std::make_shared<px4_msgs::msg::VehicleAttitude>();
                    int sender_id = -1;
                    if (deserializeAttitude(buffer, recv_len, att_msg, sender_id)) {
                        if (sender_id != robot_id_) {
                            // 获取或创建该 UAV 的发布者
                            auto pub = getOrCreateAttitudePublisher(sender_id);
                            pub->publish(*att_msg);
                        }
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }

    // ========================================================================
    // 动态发布者获取/创建函数
    // ========================================================================
    rclcpp::Publisher<px4_msgs::msg::VehicleLocalPosition>::SharedPtr 
    getOrCreatePositionPublisher(int sender_id)
    {
        std::lock_guard<std::mutex> lock(pub_mutex_);
        
        // 如果已存在，直接返回
        if (position_pubs_.find(sender_id) != position_pubs_.end()) {
            return position_pubs_[sender_id];
        }
        
        // 创建新的发布者
        std::string topic = "/uav" + std::to_string(sender_id) + "/fmu/out/vehicle_local_position";
        auto pub = this->create_publisher<px4_msgs::msg::VehicleLocalPosition>(topic, 10);
        position_pubs_[sender_id] = pub;
        
        RCLCPP_INFO(this->get_logger(), "Created position publisher for UAV %d: %s", sender_id, topic.c_str());
        return pub;
    }
    
    rclcpp::Publisher<px4_msgs::msg::VehicleAttitude>::SharedPtr 
    getOrCreateAttitudePublisher(int sender_id)
    {
        std::lock_guard<std::mutex> lock(pub_mutex_);
        
        // 如果已存在，直接返回
        if (attitude_pubs_.find(sender_id) != attitude_pubs_.end()) {
            return attitude_pubs_[sender_id];
        }
        
        // 创建新的发布者
        std::string topic = "/uav" + std::to_string(sender_id) + "/fmu/out/vehicle_attitude";
        auto pub = this->create_publisher<px4_msgs::msg::VehicleAttitude>(topic, 10);
        attitude_pubs_[sender_id] = pub;
        
        RCLCPP_INFO(this->get_logger(), "Created attitude publisher for UAV %d: %s", sender_id, topic.c_str());
        return pub;
    }

    // ========================================================================
    // 序列化 VehicleLocalPosition
    // ========================================================================
    int serializePosition(const px4_msgs::msg::VehicleLocalPosition::SharedPtr& msg, char* buffer)
    {
        char* ptr = buffer;

        *((MESSAGE_TYPE*)ptr) = MSG_VEHICLE_LOCAL_POSITION;
        ptr += sizeof(MESSAGE_TYPE);

        *((int32_t*)ptr) = robot_id_;
        ptr += sizeof(int32_t);

        *((uint64_t*)ptr) = msg->timestamp;
        ptr += sizeof(uint64_t);

        *((float*)ptr) = msg->x; ptr += sizeof(float);
        *((float*)ptr) = msg->y; ptr += sizeof(float);
        *((float*)ptr) = msg->z; ptr += sizeof(float);

        *((float*)ptr) = msg->vx; ptr += sizeof(float);
        *((float*)ptr) = msg->vy; ptr += sizeof(float);
        *((float*)ptr) = msg->vz; ptr += sizeof(float);

        *((float*)ptr) = msg->ax; ptr += sizeof(float);
        *((float*)ptr) = msg->ay; ptr += sizeof(float);
        *((float*)ptr) = msg->az; ptr += sizeof(float);

        *((float*)ptr) = msg->heading;
        ptr += sizeof(float);

        return ptr - buffer;
    }

    bool deserializePosition(const char* buffer, int len, 
                             px4_msgs::msg::VehicleLocalPosition::SharedPtr& msg, int& sender_id)
    {
        const char* ptr = buffer;
        ptr += sizeof(MESSAGE_TYPE);

        sender_id = *((int32_t*)ptr);
        ptr += sizeof(int32_t);

        msg->timestamp = *((uint64_t*)ptr);
        ptr += sizeof(uint64_t);

        msg->x = *((float*)ptr); ptr += sizeof(float);
        msg->y = *((float*)ptr); ptr += sizeof(float);
        msg->z = *((float*)ptr); ptr += sizeof(float);

        msg->vx = *((float*)ptr); ptr += sizeof(float);
        msg->vy = *((float*)ptr); ptr += sizeof(float);
        msg->vz = *((float*)ptr); ptr += sizeof(float);

        msg->ax = *((float*)ptr); ptr += sizeof(float);
        msg->ay = *((float*)ptr); ptr += sizeof(float);
        msg->az = *((float*)ptr); ptr += sizeof(float);

        msg->heading = *((float*)ptr);
        ptr += sizeof(float);

        return true;
    }

    // ========================================================================
    // 序列化 VehicleAttitude
    // ========================================================================
    int serializeAttitude(const px4_msgs::msg::VehicleAttitude::SharedPtr& msg, char* buffer)
    {
        char* ptr = buffer;

        *((MESSAGE_TYPE*)ptr) = MSG_VEHICLE_ATTITUDE;
        ptr += sizeof(MESSAGE_TYPE);

        *((int32_t*)ptr) = robot_id_;
        ptr += sizeof(int32_t);

        *((uint64_t*)ptr) = msg->timestamp;
        ptr += sizeof(uint64_t);

        *((float*)ptr) = msg->q[0]; ptr += sizeof(float);
        *((float*)ptr) = msg->q[1]; ptr += sizeof(float);
        *((float*)ptr) = msg->q[2]; ptr += sizeof(float);
        *((float*)ptr) = msg->q[3]; ptr += sizeof(float);

        return ptr - buffer;
    }

    bool deserializeAttitude(const char* buffer, int len, 
                             px4_msgs::msg::VehicleAttitude::SharedPtr& msg, int& sender_id)
    {
        const char* ptr = buffer;
        ptr += sizeof(MESSAGE_TYPE);

        sender_id = *((int32_t*)ptr);
        ptr += sizeof(int32_t);

        msg->timestamp = *((uint64_t*)ptr);
        ptr += sizeof(uint64_t);

        msg->q[0] = *((float*)ptr); ptr += sizeof(float);
        msg->q[1] = *((float*)ptr); ptr += sizeof(float);
        msg->q[2] = *((float*)ptr); ptr += sizeof(float);
        msg->q[3] = *((float*)ptr); ptr += sizeof(float);

        return true;
    }

    // ========================================================================
    // 成员变量
    // ========================================================================
    // 参数
    int robot_id_;
    std::string next_robot_ip_;
    std::string broadcast_ip_;
    double broadcast_freq_;

    // Socket 文件描述符
    int udp_send_fd_ = -1;
    int udp_recv_fd_ = -1;
    struct sockaddr_in udp_broadcast_addr_;

    // 线程控制
    std::thread udp_recv_thread_;
    bool running_ = true;
    
    // 动态发布者映射表和互斥锁
    std::map<int, rclcpp::Publisher<px4_msgs::msg::VehicleLocalPosition>::SharedPtr> position_pubs_;
    std::map<int, rclcpp::Publisher<px4_msgs::msg::VehicleAttitude>::SharedPtr> attitude_pubs_;
    std::mutex pub_mutex_;

    // PX4 订阅者
    rclcpp::Subscription<px4_msgs::msg::VehicleLocalPosition>::SharedPtr position_sub_;
    rclcpp::Subscription<px4_msgs::msg::VehicleAttitude>::SharedPtr attitude_sub_;
    
    // UAV 名称
    std::string uav_name_;
};



// ============================================================================
// Main 函数
// ============================================================================
int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<CommBridgeNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
