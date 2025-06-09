#include "GameServer.h"
#include "ClientSession.h"
#include <spdlog/spdlog.h>
#include <random>

namespace Game {

// 单例实例的实现
GameServer& GameServer::Instance() {
    static GameServer instance;
    return instance;
}

// 私有构造函数
GameServer::GameServer()
    : m_acceptor(m_ioContext),
      m_signals(m_ioContext, SIGINT, SIGTERM),
      m_broadcastTimer(m_ioContext),
      m_gameCountdownTimer(m_ioContext)
{
    // 设置信号处理，用于优雅关闭服务器
    m_signals.async_wait([this](const asio::error_code&, int) {
        spdlog::warn("Shutdown signal received. Stopping server...");
        Stop();
    });
}

GameServer::~GameServer() {
    Stop();
}

void GameServer::Start(unsigned short port) {
    spdlog::info("Game Server starting on port {}", port);

    // 预先生成食物
    for (int i = 0; i < MAX_FOODS_ON_SCREEN; ++i) {
        SpawnNewFood(false);
    }
    
    asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port);
    m_acceptor.open(endpoint.protocol());
    m_acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    m_acceptor.bind(endpoint);
    m_acceptor.listen();

    DoAccept(); // 开始接受连接

    // 创建线程池来运行 io_context，充分利用多核CPU
    // 通常线程数等于硬件并发线程数
    unsigned int thread_count = std::thread::hardware_concurrency();
    thread_count = (thread_count > 0) ? thread_count : 2; // 至少2个线程
    spdlog::info("Starting server with {} worker threads.", thread_count);

    for (unsigned int i = 0; i < thread_count; ++i) {
        m_threadPool.emplace_back([this]() {
            m_ioContext.run();
        });
    }
}

void GameServer::Stop() {
    // 停止 io_context 可以让 run() 调用返回，从而结束线程
    if (!m_ioContext.stopped()) {
        if(m_gameRunning) EndGame();
        
        m_broadcastTimer.cancel();
        m_gameCountdownTimer.cancel();
        m_acceptor.close();
        
        m_ioContext.stop();

        for (auto& t : m_threadPool) {
            if (t.joinable()) {
                t.join();
            }
        }
        m_threadPool.clear();
        spdlog::info("Server stopped.");
    }
}

void GameServer::DoAccept() {
    // 异步接受新连接
    m_acceptor.async_accept([this](const asio::error_code& ec, asio::ip::tcp::socket socket) {
        if (!ec) {
            spdlog::info("New client connected from: {}", socket.remote_endpoint().address().to_string());
            // 使用 std::make_shared 创建 ClientSession，并启动它
            std::make_shared<ClientSession>(std::move(socket), *this)->Start();
        } else {
            spdlog::error("Accept error: {}", ec.message());
        }
        // 如果 acceptor 仍然打开，继续接受下一个连接
        if (m_acceptor.is_open()) {
            DoAccept();
        }
    });
}

// 处理新客户端连接请求
void GameServer::HandleClientConnect(std::shared_ptr<ClientSession> session, const std::string& playerName) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (m_players.size() >= MAX_PLAYERS) {
        spdlog::warn("Client connection rejected, server full. Name: {}", playerName);
        nlohmann::json fullMsg = {{"type", "SERVER_FULL"}};
        session->SendMessage(fullMsg.dump());
        // Session 会在发送后自行关闭
        return;
    }

    int playerId = m_nextPlayerId.fetch_add(1);
    session->SetPlayerId(playerId);

    std::string playerColor = m_playerColors[m_clients.size() % m_playerColors.size()];

    // 创建新玩家数据
    PlayerData newPlayer(playerId, playerName, SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f, playerColor);
    m_players[playerId] = newPlayer;
    m_clients[playerId] = session;

    // 发送欢迎消息，包含初始游戏状态
    nlohmann::json welcomeMsg = {
        {"type", "WELCOME"},
        {"playerId", playerId},
        {"initialGameState", GetCurrentGameStateJson()}
    };
    session->SendMessage(welcomeMsg.dump());

    // 向其他玩家广播新玩家加入的消息
    nlohmann::json playerJoinedMsg = {
        {"type", "PLAYER_JOINED"},
        {"player", newPlayer}
    };
    BroadcastMessage(playerJoinedMsg.dump(), session);
    
    spdlog::info("Player {} ({}) joined. Total clients: {}", playerId, playerName, m_clients.size());

    // 如果游戏未开始且有玩家，则开始游戏
    if (!m_gameRunning && !m_clients.empty()) {
        StartGame();
    }
}

void GameServer::RemoveClient(std::shared_ptr<ClientSession> session) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    int playerId = session->GetPlayerId();
    if (playerId == -1) return;

    if (m_clients.erase(playerId) > 0) {
        m_players.erase(playerId);
        
        nlohmann::json playerLeftMsg = {
            {"type", "PLAYER_LEFT"},
            {"playerId", playerId}
        };
        BroadcastMessage(playerLeftMsg.dump());
        spdlog::info("Player {} removed. Total clients: {}", playerId, m_clients.size());
    }

    // 如果所有玩家都离开，则结束游戏
    if (m_clients.empty() && m_gameRunning) {
        spdlog::info("All clients disconnected, ending game.");
        EndGame();
    }
}

// 广播消息
void GameServer::BroadcastMessage(const std::string& message, std::shared_ptr<ClientSession> exclude_session) {
    // 因为 m_clients 可能在其他线程被修改，需要保护
    // 这里我们复制一份需要发送的会话列表，避免在迭代时持有锁太久
    std::vector<std::shared_ptr<ClientSession>> recipients;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        for(const auto& pair : m_clients) {
            if(pair.second != exclude_session) {
                recipients.push_back(pair.second);
            }
        }
    }

    for (const auto& client : recipients) {
        client->SendMessage(message);
    }
}

void GameServer::UpdatePlayerPosition(int playerId, float x, float y) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_players.find(playerId);
    if (it != m_players.end()) {
        it->second.x = x;
        it->second.y = y;
    }
}

void GameServer::HandlePlayerAteFood(int playerId, int foodId) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    auto player_it = m_players.find(playerId);
    if (player_it == m_players.end()) return;

    auto& player = player_it->second;

    auto food_it = std::find_if(m_foods.begin(), m_foods.end(), [foodId](const FoodData& food) {
        return food.id == foodId;
    });

    if (food_it != m_foods.end()) {
        float dx = player.x - food_it->x;
        float dy = player.y - food_it->y;
        float distance = std::sqrt(dx * dx + dy * dy);

        if (distance < PLAYER_RADIUS + FoodData::RADIUS) {
            // 从列表中移除食物
            FoodData eatenFood = *food_it;
            m_foods.erase(food_it);

            player.score += 10;
            spdlog::info("Player {} ate food {}. New score: {}", playerId, foodId, player.score);

            nlohmann::json foodEatenMsg = {
                {"type", "FOOD_EATEN"},
                {"foodId", eatenFood.id},
                {"eaterPlayerId", playerId},
                {"newScore", player.score}
            };
            BroadcastMessage(foodEatenMsg.dump());
            
            SpawnNewFood(true);
        } else {
             spdlog::warn("Player {} reported eating food {} but server collision check failed. Dist: {}", playerId, foodId, distance);
        }
    }
}


void GameServer::SpawnNewFood(bool broadcast) {
    if (m_foods.size() < MAX_FOODS_ON_SCREEN) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> x_dist(FoodData::RADIUS, SCREEN_WIDTH - FoodData::RADIUS);
        std::uniform_real_distribution<> y_dist(FoodData::RADIUS, SCREEN_HEIGHT - FoodData::RADIUS);

        FoodData newFood;
        newFood.id = m_nextFoodId.fetch_add(1);
        newFood.x = static_cast<float>(x_dist(gen));
        newFood.y = static_cast<float>(y_dist(gen));
        
        m_foods.push_back(newFood);

        if (broadcast) {
            nlohmann::json foodSpawnedMsg = {
                {"type", "FOOD_SPAWNED"},
                {"food", newFood}
            };
            BroadcastMessage(foodSpawnedMsg.dump());
        }
    }
}


// --- 游戏流程控制 ---

nlohmann::json GameServer::GetCurrentGameStateJson() {
    // 注意：这个函数应该在持有锁的情况下被调用
    nlohmann::json state;
    state["timer"] = m_remainingTimeSeconds.load();
    state["players"] = nlohmann::json::array();
    for(const auto& pair : m_players) {
        state["players"].push_back(pair.second);
    }
    state["foods"] = m_foods;
    return state;
}

void GameServer::StartGame() {
    if (m_gameRunning) {
        spdlog::info("StartGame called but game already running.");
        return;
    }
    spdlog::info("Starting a new game...");
    
    m_remainingTimeSeconds = GAME_DURATION_SECONDS;
    m_gameRunning = true;

    // 重置分数和食物
    for (auto& pair : m_players) {
        pair.second.score = 0;
    }
    m_foods.clear();
    for (int i = 0; i < MAX_FOODS_ON_SCREEN; ++i) {
        SpawnNewFood(false);
    }

    // 启动游戏倒计时
    m_gameCountdownTimer.expires_at(std::chrono::steady_clock::now() + std::chrono::seconds(1));
    m_gameCountdownTimer.async_wait([this](const asio::error_code& ec){ this->UpdateGameTimer(ec); });
    
    // 启动状态广播
    m_broadcastTimer.expires_at(std::chrono::steady_clock::now() + std::chrono::milliseconds(BROADCAST_INTERVAL_MS));
    m_broadcastTimer.async_wait([this](const asio::error_code& ec){ this->BroadcastGameState(ec); });
}

void GameServer::EndGame() {
    if (!m_gameRunning) return;
    m_gameRunning = false; // 必须先设置，以停止定时器循环
    spdlog::info("Ending game.");

    m_gameCountdownTimer.cancel();
    m_broadcastTimer.cancel();

    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    PlayerData* winner = nullptr;
    int maxScore = -1;
    nlohmann::json finalScores = nlohmann::json::array();

    for (auto& pair : m_players) {
        finalScores.push_back({
            {"id", pair.second.id},
            {"name", pair.second.name},
            {"score", pair.second.score}
        });

        if (pair.second.score > maxScore) {
            maxScore = pair.second.score;
            winner = &pair.second;
        } else if (pair.second.score == maxScore && maxScore != -1) {
            winner = nullptr; //
        }
    }

    nlohmann::json gameOverMsg;
    gameOverMsg["type"] = "GAME_OVER";
    gameOverMsg["winnerId"] = winner ? winner->id : -1;
    gameOverMsg["winnerName"] = winner ? winner->name : (maxScore != -1 ? "Tie" : "N/A");
    gameOverMsg["scores"] = finalScores;

    BroadcastMessage(gameOverMsg.dump());
    spdlog::info("GAME_OVER message broadcast.");
}


void GameServer::UpdateGameTimer(const asio::error_code& ec) {
    if (ec || !m_gameRunning) { // 如果定时器被取消或游戏结束，则返回
        if (ec != asio::error::operation_aborted) spdlog::error("Game timer error: {}", ec.message());
        return;
    }

    if (m_remainingTimeSeconds > 0) {
        m_remainingTimeSeconds--;
    }

    if (m_remainingTimeSeconds <= 0) {
        EndGame();
    } else {
        // 重新设置定时器
        m_gameCountdownTimer.expires_at(m_gameCountdownTimer.expiry() + std::chrono::seconds(1));
        m_gameCountdownTimer.async_wait([this](const asio::error_code& ec){ this->UpdateGameTimer(ec); });
    }
}

void GameServer::BroadcastGameState(const asio::error_code& ec) {
    if (ec || !m_gameRunning) {
        if (ec != asio::error::operation_aborted) spdlog::error("Broadcast timer error: {}", ec.message());
        return;
    }

    nlohmann::json stateUpdate;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        stateUpdate["type"] = "GAME_STATE_UPDATE";
        stateUpdate.update(GetCurrentGameStateJson());
    }
    BroadcastMessage(stateUpdate.dump());

    // 重新设置广播定时器
    m_broadcastTimer.expires_at(m_broadcastTimer.expiry() + std::chrono::milliseconds(BROADCAST_INTERVAL_MS));
    m_broadcastTimer.async_wait([this](const asio::error_code& ec){ this->BroadcastGameState(ec); });
}

} // namespace Game