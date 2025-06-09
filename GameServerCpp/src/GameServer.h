#pragma once

#include <asio.hpp>
#include <asio/steady_timer.hpp>
#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include "DataTypes.h"

// 前向声明，避免在头文件中包含 ClientSession.h，减少编译依赖
class ClientSession;

namespace Game {

// GameServer 采用单例模式，确保全局只有一个游戏世界实例
class GameServer {
public:
    // 获取单例实例的静态方法
    static GameServer& Instance();

    // 禁止拷贝和赋值
    GameServer(const GameServer&) = delete;
    GameServer& operator=(const GameServer&) = delete;

    // 启动服务器，监听指定端口
    void Start(unsigned short port);

    // 停止服务器
    void Stop();
    
    // --- 由 ClientSession 调用的公共接口 ---
    void HandleClientConnect(std::shared_ptr<ClientSession> session, const std::string& playerName);
    void RemoveClient(std::shared_ptr<ClientSession> session);
    void UpdatePlayerPosition(int playerId, float x, float y);
    void HandlePlayerAteFood(int playerId, int foodId);
    
    // 向所有客户端广播消息，可选择排除一个
    void BroadcastMessage(const std::string& message, std::shared_ptr<ClientSession> exclude_session = nullptr);

private:
    // 私有构造函数，防止外部直接创建实例
    GameServer();
    // 私有析构函数
    ~GameServer();
    
    // --- 内部异步操作 ---
    void DoAccept(); // 开始接受新的客户端连接
    void OnAccept(const asio::error_code& ec, asio::ip::tcp::socket socket); // 接受连接后的回调

    // --- 游戏逻辑与定时器 ---
    void StartGame();
    void EndGame();
    void UpdateGameTimer(const asio::error_code& ec);
    void BroadcastGameState(const asio::error_code& ec);
    void SpawnNewFood(bool broadcast);
    nlohmann::json GetCurrentGameStateJson();

private:
    // --- 常量配置 ---
    static constexpr int MAX_PLAYERS = 3;
    static constexpr int GAME_DURATION_SECONDS = 60;
    static constexpr int SCREEN_WIDTH = 2400;
    static constexpr int SCREEN_HEIGHT = 1600;
    static constexpr int MAX_FOODS_ON_SCREEN = 50;
    static constexpr float PLAYER_RADIUS = 30.0f;
    static constexpr long BROADCAST_INTERVAL_MS = 50;

    // --- Asio 核心组件 ---
    asio::io_context m_ioContext; // 异步I/O事件循环
    asio::ip::tcp::acceptor m_acceptor; // 监听和接受TCP连接
    asio::signal_set m_signals; // 处理系统信号，用于优雅停机
    asio::steady_timer m_broadcastTimer; // 定时广播游戏状态
    asio::steady_timer m_gameCountdownTimer; // 游戏倒计时
    std::vector<std::thread> m_threadPool; // 线程池来运行 io_context

    // --- 游戏状态 (线程安全) ---
    std::mutex m_mutex; // 保护所有游戏状态数据
    std::unordered_map<int, std::shared_ptr<ClientSession>> m_clients;
    std::unordered_map<int, PlayerData> m_players;
    std::vector<FoodData> m_foods;

    std::atomic<int> m_nextPlayerId{0};
    std::atomic<int> m_nextFoodId{0};
    
    std::atomic<int> m_remainingTimeSeconds{GAME_DURATION_SECONDS};
    std::atomic<bool> m_gameRunning{false};

    const std::vector<std::string> m_playerColors = {"#FF0000", "#00FF00", "#0000FF", "#FFFF00", "#FF00FF"};
};

} // namespace Game