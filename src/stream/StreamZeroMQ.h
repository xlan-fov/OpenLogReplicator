/* ZeroMQ通信流处理类
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

#ifndef STREAM_ZEROMQ_H_
#define STREAM_ZEROMQ_H_

#include "Stream.h"

#ifdef LINK_LIBRARY_ZEROMQ
#include <zmq.h>

namespace OpenLogReplicator {
    // ZeroMQ流通信类 - 处理基于ZeroMQ库的消息传递
    class StreamZeroMQ : public Stream {
    protected:
        std::string uri;           // 服务器URI地址
        void* context;             // ZeroMQ上下文
        void* socket;              // ZeroMQ套接字

    public:
        // 构造与析构
        StreamZeroMQ(Ctx* newCtx, const char* newUri);
        ~StreamZeroMQ() override;

        // 初始化方法
        void initialize() override;
        void initializeServer() override;
        void initializeClient() override;

        // ZeroMQ消息传输方法
        void sendMessage(const char* buffer, uint64_t length) override;
        uint64_t receiveMessage(uint8_t* buffer, uint64_t maxLength) override;
        void receiveMessageAll(uint8_t* buffer, uint64_t length) override;
        void clientDisconnect() override;
    };
}

#endif /* LINK_LIBRARY_ZEROMQ */
#endif
