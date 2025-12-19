cmake_minimum_required(VERSION 3.8)
project(multi_robot_comm)

# C++ 17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE "Release")

add_compile_options(-Wall -O3)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(px4_msgs REQUIRED)

# Include directories
include_directories(
  include
)

# Add executable
add_executable(comm_bridge_node
  src/comm_bridge_node.cpp
)

ament_target_dependencies(comm_bridge_node
  rclcpp
  std_msgs
  geometry_msgs
  nav_msgs
  px4_msgs
)

# Install targets
install(
  TARGETS comm_bridge_node
  DESTINATION lib/${PROJECT_NAME}
)




# Install launch files
install(
  DIRECTORY launch/
  DESTINATION share/${PROJECT_NAME}/launch
)

# Install include directory
install(
  DIRECTORY include/
  DESTINATION include
)

ament_package()
