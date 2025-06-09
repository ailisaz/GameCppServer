#pragma once

#include <asio.hpp>
#include <memory>
#include <deque>
#include <string>

namespace Game { class GameServer; } // 前向声明

// ClientSession 代表一个客户端连接
// 它继承自 enable_shared_from_this，这是 Asio 中管理会话生命周期的常用模式
// 只要有异步操作在进行，指向此对象的 shared_ptr 就会存在，防止对象被过早销毁
class ClientSession : public std::enable_shared_from_this<ClientSession> {
public:
    // 构造函数，接收一个已连接的 socket 和 GameServer 的引用
    ClientSession(asio::ip::tcp::socket socket, Game::GameServer& server);

    // 启动会话，开始读取数据
    void Start();

    // 向客户端发送消息
    void SendMessage(const std::string& message);

    // 设置和获取玩家ID
    void SetPlayerId(int id) { m_playerId = id; }
    int GetPlayerId() const { return m_playerId; }

private:
    // 异步读取操作
    void DoRead();
    // 读取完成后的回调
    void OnRead(const asio::error_code& ec, size_t bytes_transferred);
    // 处理收到的完整消息
    void HandleMessage(const std::string& message);
    
    // 异步写入操作
    void DoWrite();
    // 写入完成后的回调
    void OnWrite(const asio::error_code& ec, size_t bytes_transferred);
    
    // 关闭连接
    void Close();

private:
    asio::ip::tcp::socket m_socket;     // 此会话的socket
    Game::GameServer& m_server;         // GameServer的引用
    asio::streambuf m_readBuffer;       // 用于读取数据的缓冲区
    std::deque<std::string> m_writeQueue; // 待发送消息的队列
    int m_playerId = -1;                // 玩家ID，-1表示尚未分配
};