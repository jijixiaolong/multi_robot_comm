"""
PX4 Multi-Robot Communication Bridge Launch File

用法:
    # UAV1 (robot_id=1)
    ros2 launch multi_robot_comm comm_bridge.launch.py robot_id:=1 uav_name:=uav1 broadcast_ip:=192.168.1.255
    
    # UAV2 (robot_id=2)
    ros2 launch multi_robot_comm comm_bridge.launch.py robot_id:=2 uav_name:=uav2 broadcast_ip:=192.168.1.255
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    robot_id_arg = DeclareLaunchArgument(
        'robot_id',
        default_value='1',
        description='Unique ID of this robot (1, 2, 3, ...)'
    )
    
    uav_name_arg = DeclareLaunchArgument(
        'uav_name',
        default_value='uav1',
        description='UAV name for topic namespacing (uav1, uav2, uav3, ...)'
    )
    
    broadcast_ip_arg = DeclareLaunchArgument(
        'broadcast_ip',
        default_value='127.0.0.255',
        description='UDP broadcast IP address'
    )
    
    broadcast_freq_arg = DeclareLaunchArgument(
        'broadcast_freq',
        default_value='50.0',
        description='Broadcast frequency in Hz'
    )

    # PX4 communication bridge node
    comm_bridge_node = Node(
        package='multi_robot_comm',
        executable='comm_bridge_node',
        name='comm_bridge_node',
        output='screen',
        parameters=[{
            'robot_id': LaunchConfiguration('robot_id'),
            'uav_name': LaunchConfiguration('uav_name'),
            'broadcast_ip': LaunchConfiguration('broadcast_ip'),
            'broadcast_freq': LaunchConfiguration('broadcast_freq'),
            'position_topic_suffix': '/fmu/out/vehicle_local_position',
            'attitude_topic_suffix': '/fmu/out/vehicle_attitude',
        }]
    )

    return LaunchDescription([
        robot_id_arg,
        uav_name_arg,
        broadcast_ip_arg,
        broadcast_freq_arg,
        comm_bridge_node,
    ])
