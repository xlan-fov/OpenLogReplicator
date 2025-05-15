/* 流式写入器类头文件
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

#ifndef WRITER_STREAM_H_
#define WRITER_STREAM_H_

#include "Writer.h"
#include "../common/OraProtoBuf.pb.h"

namespace OpenLogReplicator {
    class Stream;

    // 流式写入器类 - 负责将重做日志数据通过网络流发送给客户端
    class WriterStream final : public Writer {
    protected:
        Stream* stream;                  // 流对象指针
        pb::RedoRequest request;         // 请求协议缓冲区
        pb::RedoResponse response;       // 响应协议缓冲区

        std::string getType() const override;          // 获取写入器类型名称
        void processInfo();                            // 处理INFO请求
        void processStart();                           // 处理START请求
        void processContinue();                        // 处理CONTINUE请求
        void processConfirm();                         // 处理CONFIRM请求
        void pollQueue() override;                     // 轮询消息队列
        void sendMessage(BuilderMsg* msg) override;    // 发送消息

    public:
        // 构造函数
        WriterStream(Ctx* newCtx, std::string newAlias, std::string newDatabase, Builder* newBuilder, Metadata* newMetadata, Stream* newStream);
        // 析构函数
        ~WriterStream() override;

        // 初始化方法
        void initialize() override;
    };
}

#endif
