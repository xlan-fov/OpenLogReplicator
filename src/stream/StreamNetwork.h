/* TCP/IP网络通信流处理类
   Copyright (C) 2018-2025 Adam Leszczynski (aleszczynski@bersler.com)

This file is part of OpenLogReplicator.

OpenLogReplicator is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as published
by the Free Software Foundation; either version 3, or (at your option)
any later version.

OpenLogReplicator is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenLogReplicator; see the file LICENSE;  If not see
<http://www.gnu.org/licenses/>.  */

#ifndef STREAM_NETWORK_H_
#define STREAM_NETWORK_H_

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include "Stream.h"

namespace OpenLogReplicator {
    // 网络流通信类 - 处理基于TCP/IP套接字的数据传输
    class StreamNetwork : public Stream {
    protected:
        std::string uri;         // 服务器URI地址
        int port;                // 端口号
        int serverSocketfd;      // 服务器套接字文件描述符
        int clientSocketfd;      // 客户端套接字文件描述符
        struct sockaddr_in server; // 服务器地址结构
        struct sockaddr_in client; // 客户端地址结构
        socklen_t clientLength;   // 客户端地址结构长度

        // 私有方法
        void prepareUri();        // 准备URI配置

    public:
        // 构造与析构
        StreamNetwork(Ctx* newCtx, const char* newUri);
        ~StreamNetwork() override;

        // 初始化方法
        void initialize() override;
        void initializeServer() override;
        void initializeClient() override;

        // 网络数据传输方法
        void sendMessage(const char* buffer, uint64_t length) override;
        uint64_t receiveMessage(uint8_t* buffer, uint64_t maxLength) override;
        void receiveMessageAll(uint8_t* buffer, uint64_t length) override;
        void clientDisconnect() override;
    };
}

#endif
