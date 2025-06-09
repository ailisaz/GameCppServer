# --- 基础配置 ---
# CMake 最低版本要求
cmake_minimum_required(VERSION 3.16)

# 项目名称和语言
project(GameServerCpp VERSION 1.0 LANGUAGES CXX)

# 设置C++标准为C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# --- 查找依赖项 (vcpkg 将会帮助找到它们) ---
# 这是现代CMake的做法，比单纯包含头文件目录更健壮
# REQUIRED 参数意味着如果找不到包，配置会直接失败，这能帮助我们尽早发现问题
find_package(asio REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)


# --- 定义可执行文件 ---
# 添加可执行文件目标，并列出所有源文件
add_executable(GameServer
    src/main.cpp
    src/GameServer.cpp
    src/ClientSession.cpp
)


# --- 链接依赖项到目标 ---
# 这是最关键的一步，它会自动处理包含目录(include directories)和库文件(libraries)
# 将我们找到的包链接到 GameServer 这个目标上
# asio::asio, spdlog::spdlog 是由 find_package 创建的导入目标(imported targets)
target_link_libraries(GameServer PRIVATE
    asio::asio
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

# 在Windows上，Asio可能需要链接系统库
if(WIN32)
    target_link_libraries(GameServer PRIVATE ws2_32 wsock32)
endif()

# 在Linux上，需要链接pthread
if (UNIX AND NOT APPLE)
    target_link_libraries(GameServer PRIVATE pthread)
endif()


# --- 输出信息 ---
# 打印消息，方便调试
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Configuring done. To build, use the Build button in VS Code or run 'cmake --build .'")