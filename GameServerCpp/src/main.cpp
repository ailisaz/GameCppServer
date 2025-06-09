#include "GameServer.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

constexpr unsigned short DEFAULT_PORT = 12345;

int main() {
    try {
        // 初始化日志记录器
        spdlog::set_level(spdlog::level::trace); // 设置最低日志级别
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v");
        spdlog::info("Application starting...");

        // 获取 GameServer 单例并启动它
        Game::GameServer::Instance().Start(DEFAULT_PORT);

        // 主线程将在此处阻塞，等待服务器关闭
        // 实际上，所有工作都在 GameServer 的线程池中进行
        // 为了让 main 函数不立即退出，我们可以等待 GameServer 的 Stop() 方法被调用
        // 一个简单的方法是让主线程也加入到 io_context 的运行中，或者用一个条件变量等待
        // 但是我们的设计是将 io_context.run() 放在线程池里了，主线程可以做其他事或直接等待
        std::promise<void> promise;
        auto future = promise.get_future();
        future.wait(); // 这将永远等待，因为我们没有设置promise的值，服务器将通过信号关闭
        
    } catch (const std::exception& e) {
        spdlog::critical("An unhandled exception occurred: {}", e.what());
        return 1;
    }

    spdlog::info("Application finished.");
    return 0;
}