/* 基础流通信抽象类
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

#ifndef STREAM_H_
#define STREAM_H_

#include <cstdint>
#include <string>

namespace OpenLogReplicator {
    // 前向声明
    class Ctx;

    // 流通信抽象基类 - 定义通信接口
    class Stream {
    protected:
        Ctx* ctx;             // 上下文对象

    public:
        // 构造与析构
        explicit Stream(Ctx* newCtx);
        virtual ~Stream();

        // 初始化接口 - 纯虚函数
        virtual void initialize() = 0;
        virtual void initializeServer() = 0;
        virtual void initializeClient() = 0;

        // 消息传输接口 - 纯虚函数
        virtual void sendMessage(const char* buffer, uint64_t length) = 0;
        virtual uint64_t receiveMessage(uint8_t* buffer, uint64_t maxLength) = 0;
        virtual void receiveMessageAll(uint8_t* buffer, uint64_t length) = 0;
        virtual void clientDisconnect() = 0;
    };
}

#endif
