/* 写入器基类头文件
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

#ifndef WRITER_H_
#define WRITER_H_

#include "../common/Thread.h"
#include "../common/types/Scn.h"

namespace OpenLogReplicator {
    class Builder;
    class BuilderMsg;
    class BuilderQueue;
    class Metadata;

    // 写入器基类 - 所有输出写入器的基础类
    class Writer : public Thread {
    protected:
        static constexpr uint64_t CHECKPOINT_FILE_MAX_SIZE = 1024;  // 检查点文件最大大小

        std::string database;              // 数据库名称
        Builder* builder;                  // 构建器指针
        Metadata* metadata;                // 元数据指针
        
        // 本地检查点信息
        BuilderQueue* builderQueue{nullptr};  // 构建器队列
        Scn checkpointScn{Scn::none()};    // 检查点SCN
        typeIdx checkpointIdx{0};          // 检查点索引
        time_t checkpointTime;             // 检查点时间
        
        // 消息统计和状态
        uint64_t sentMessages{0};          // 已发送消息数
        uint64_t oldSize{0};               // 旧大小
        uint64_t currentQueueSize{0};      // 当前队列大小
        uint64_t hwmQueueSize{0};          // 队列大小高水位
        bool streaming{false};             // 是否正在流式传输
        bool redo{false};                  // 是否重做

        // 线程同步
        std::mutex mtx;                    // 互斥锁
        // 客户端确认信息
        Scn confirmedScn{Scn::none()};     // 已确认SCN
        typeIdx confirmedIdx{0};           // 已确认索引
        BuilderMsg** queue{nullptr};       // 消息队列

        // 核心方法
        void createMessage(BuilderMsg* msg);            // 创建消息
        virtual void sendMessage(BuilderMsg* msg) = 0;  // 发送消息（子类实现）
        virtual std::string getType() const = 0;        // 获取写入器类型（子类实现）
        virtual void pollQueue() = 0;                   // 轮询队列（子类实现）
        void run() override;                            // 线程运行方法
        void mainLoop();                                // 主循环
        virtual void writeCheckpoint(bool force);       // 写入检查点
        void readCheckpoint();                          // 读取检查点
        void sortQueue();                               // 排序队列
        void resetMessageQueue();                       // 重置消息队列

    public:
        // 构造函数
        Writer(Ctx* newCtx, std::string newAlias, std::string newDatabase, Builder* newBuilder, Metadata* newMetadata);
        // 析构函数
        virtual ~Writer();

        virtual void initialize();                     // 初始化
        void confirmMessage(BuilderMsg* msg);         // 确认消息
        void wakeUp() override;                       // 唤醒线程
        virtual void flush() {};                      // 刷新数据（可选实现）
    };
}

#endif
