#include "ClientSession.h"
#include "GameServer.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

ClientSession::ClientSession(asio::ip::tcp::socket socket, Game::GameServer& server)
    : m_socket(std::move(socket)), m_server(server) {}

void ClientSession::Start() {
    // 会话开始时，立即启动第一次异步读取
    DoRead();
}

void ClientSession::SendMessage(const std::string& message) {
    // 将消息投递到 io_context 中执行，以保证线程安全
    asio::post(m_socket.get_executor(), [self = shared_from_this(), message]() {
        bool write_in_progress = !self->m_writeQueue.empty();
        self->m_writeQueue.push_back(message + "\n"); // 添加换行符作为消息分隔
        if (!write_in_progress) {
            self->DoWrite();
        }
    });
}

void ClientSession::DoRead() {
    // 异步读取数据，直到遇到换行符 '\n'
    // self = shared_from_this() 是关键，它创建了一个 shared_ptr，确保在回调函数执行前 ClientSession 对象不会被析构
    auto self = shared_from_this();
    asio::async_read_until(m_socket, m_readBuffer, '\n',
        [this, self](const asio::error_code& ec, size_t bytes_transferred) {
            OnRead(ec, bytes_transferred);
        });
}

void ClientSession::OnRead(const asio::error_code& ec, size_t bytes_transferred) {
    if (!ec) {
        // 从缓冲区中提取一行数据
        std::istream is(&m_readBuffer);
        std::string message(bytes_transferred, '\0');
        is.read(&message[0], bytes_transferred);
        message.pop_back(); // 移除末尾的换行符

        HandleMessage(message); // 处理消息

        DoRead(); // 继续下一次读取
    } else {
        // 发生错误（如客户端断开连接），关闭会话
        if (ec != asio::error::operation_aborted) {
            spdlog::info("Client {} read error: {}", m_playerId, ec.message());
            Close();
        }
    }
}

void ClientSession::HandleMessage(const std::string& message) {
    spdlog::trace("Server received from client {}: {}", m_playerId, message);
    try {
        auto json_msg = nlohmann::json::parse(message);
        std::string type = json_msg.value("type", "");

        if (type == "CONNECT" && json_msg.contains("playerName")) {
            m_server.HandleClientConnect(shared_from_this(), json_msg["playerName"]);
        } else if (m_playerId != -1) { // 确保已分配ID
            if (type == "PLAYER_UPDATE") {
                m_server.UpdatePlayerPosition(m_playerId, json_msg["x"], json_msg["y"]);
            } else if (type == "ATE_FOOD") {
                m_server.HandlePlayerAteFood(m_playerId, json_msg["foodId"]);
            }
        }
    } catch (const nlohmann::json::parse_error& e) {
        spdlog::error("Error parsing JSON from client {}: {} - {}", m_playerId, message, e.what());
    }
}


void ClientSession::DoWrite() {
    auto self = shared_from_this();
    asio::async_write(m_socket, asio::buffer(m_writeQueue.front()),
        [this, self](const asio::error_code& ec, size_t bytes_transferred) {
            OnWrite(ec, bytes_transferred);
        });
}

void ClientSession::OnWrite(const asio::error_code& ec, size_t /*bytes_transferred*/) {
    if (!ec) {
        m_writeQueue.pop_front();
        if (!m_writeQueue.empty()) {
            DoWrite(); // 如果队列中还有消息，继续发送
        }
    } else {
        if (ec != asio::error::operation_aborted) {
            spdlog::error("Client {} write error: {}", m_playerId, ec.message());
            Close();
        }
    }
}

void ClientSession::Close() {
    // 从服务器中移除此客户端
    m_server.RemoveClient(shared_from_this());
    // 关闭socket，这将取消所有挂起的异步操作
    if (m_socket.is_open()) {
        asio::error_code ec;
        m_socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        m_socket.close(ec);
    }
}