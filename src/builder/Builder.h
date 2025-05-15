/* 消息构建器类头文件
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

#ifndef BUILDER_H_
#define BUILDER_H_

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "../common/Ctx.h"
#include "../common/Format.h"
#include "../common/LobCtx.h"
#include "../common/LobData.h"
#include "../common/LobKey.h"
#include "../common/RedoLogRecord.h"
#include "../common/Thread.h"
#include "../common/exception/RedoLogException.h"
#include "../common/table/SysUser.h"
#include "../common/types/Data.h"
#include "../common/types/FileOffset.h"
#include "../common/types/LobId.h"
#include "../common/types/RowId.h"
#include "../common/types/Scn.h"
#include "../common/types/Seq.h"
#include "../common/types/Types.h"
#include "../common/types/Xid.h"
#include "../locales/CharacterSet.h"
#include "../locales/Locales.h"

namespace OpenLogReplicator {
    // 前向声明
    class BuilderJson;
    class BuilderProtobuf;
    class Column;
    class Ctx;
    class RedoLogRecord;
    class SchemaElement;
    class TransactionChunk;
    class Thread;
    class Writer;

    // 输出消息结构体
    struct BuilderMsg {
        // 输出缓冲区状态标志位
        enum class OUTPUT_BUFFER : uint64_t {
            NONE = 0,          // 无标志
            DDL = 1,           // DDL操作
            REDO = 2,          // 重做日志操作
            CHECKPOINT = 4,    // 检查点操作
            ALLOCATED = 8,     // 已分配内存
            CONFIRMED = 16     // 已确认发送
        };

        uint64_t id;           // 消息ID
        uint64_t size;         // 大小
        uint8_t* data;         // 数据指针
        Scn lwnScn;            // LWN SCN
        uint64_t lwnIdx;       // LWN索引
        Scn nextScn;           // 下一个SCN
        uint64_t flags;        // 标志位

        // 构造函数
        BuilderMsg() :
            id(0),
            size(0),
            data(nullptr),
            lwnScn(Scn::none()),
            lwnIdx(0),
            nextScn(Scn::none()),
            flags(0) {
        }

        // 设置与检查标志位
        void setFlag(OUTPUT_BUFFER flag) {
            flags |= static_cast<uint64_t>(flag);
        }

        void unsetFlag(OUTPUT_BUFFER flag) {
            flags &= ~static_cast<uint64_t>(flag);
        }

        [[nodiscard]] bool isFlagSet(OUTPUT_BUFFER flag) const {
            return (flags & static_cast<uint64_t>(flag)) != 0;
        }
    };

    // 输出缓冲区结构体
    struct BuilderQueue final {
        uint64_t id;           // 缓冲区ID
        uint64_t start;        // 起始位置
        uint64_t confirmedSize; // 已确认大小
        uint8_t* data;         // 数据指针
        BuilderQueue* next;    // 下一个队列

        // 构造函数
        BuilderQueue() :
            id(0),
            start(0),
            confirmedSize(0),
            data(nullptr),
            next(nullptr) {
        }
    };

    // 构建器基类 - 负责构建消息
    class Builder : public Thread {
    protected:
        // 常量定义
        static constexpr uint64_t FLAGS_SCHEMALESS{1};   // 无模式标志
        static constexpr uint64_t FLAGS_ADAPTIVE{2};     // 自适应标志
        static constexpr uint64_t FLAGS_KEY_AS_ARRAY{4}; // 键值作为数组标志
        
        // 状态定义
        enum class STATUS : unsigned char {
            ALLOCATE = 1,       // 分配状态
            FREE = 2,           // 释放状态
            FULL = 4,           // 满状态
            RELEASED = 8,       // 已释放状态
            CONFIRMED = 16      // 已确认状态
        };

        // 输出格式枚举
        enum class FORMAT : unsigned char {
            JSON = 0,           // JSON格式
            PROTOBUF = 1,       // Protobuf格式
            ORACLE_ORANRM_TRACE = 2 // Oracle跟踪格式
        };

        std::mutex mtx;         // 互斥锁
        std::string database;   // 数据库名称
        FORMAT format;          // 输出格式
        std::atomic<uint64_t> msgId{0}; // 消息ID
        uint64_t flags;         // 标志位
        std::map<Scn, uint64_t> scnMap; // SCN映射
        std::map<Scn, Scn> committedScn; // 已提交SCN
        std::map<Scn, uint64_t> messageIdx; // 消息索引
        uint8_t** outputBuffers; // 输出缓冲区数组
        uint64_t* outputBufferStatus; // 输出缓冲区状态数组
        BuilderQueue* lastBuilderQueue; // 最后一个队列
        BuilderJson* builderJson; // JSON构建器
        BuilderProtobuf* builderProtobuf; // Protobuf构建器
        uint64_t messagesConfirmedTotal; // 已确认消息总数
        
        BuilderQueue* getBuilderQueue(Thread* t); // 获取构建队列
        virtual void bufferFree(Thread* t, uint64_t num); // 释放缓冲区
        virtual void mainLoop() = 0; // 主循环纯虚函数

    public:
        static constexpr uint64_t OUTPUT_BUFFER_DATA_SIZE{128 * 1024 * 1024}; // 输出缓冲区大小
        BuilderQueue* firstBuilderQueue; // 第一个构建队列

        // 构造与析构函数
        // 构造函数 - 初始化构建器，设置数据库名称、别名和输出格式
        // newCtx: 上下文对象，用于访问全局配置和资源
        // newAlias: 构建器别名，用于日志记录和识别
        // newDatabase: 目标数据库名称
        // newFormat: 输出格式(JSON/Protobuf/Oracle跟踪格式)
        Builder(Ctx* newCtx, std::string newAlias, std::string newDatabase, FORMAT newFormat);
        
        // 析构函数 - 清理分配的资源，释放内存缓冲区
        ~Builder() override;

        // 初始化和运行方法
        // 初始化构建器 - 分配输出缓冲区和缓冲区状态数组
        // 在启动构建器线程前必须调用此方法
        void initialize();
        
        // 运行方法 - 重写自Thread基类，实现构建器主循环逻辑
        // 当线程启动时会调用此方法，处理消息构建和缓冲区管理
        void run() override;
        
        // 唤醒方法 - 重写自Thread基类，用于唤醒睡眠状态的构建器线程
        // 通常由其他线程调用，例如当有新事务需要处理时
        void wakeUp() override;
        
        // 配置方法
        // 设置自适应模式 - 允许构建器根据表结构变化自动调整输出模式
        // 在使用动态变化的表结构时特别有用
        void setAdaptiveSchema();
        
        // 设置无模式标志 - 允许构建器在不依赖完整表结构的情况下生成消息
        // 用于简化输出或处理特殊情况
        void setFlagSchemaless();
        
        // 设置键值作为数组标志 - 将主键表示为数组而非对象
        // 影响JSON格式输出时主键的表示方式
        void setFlagKeyAsArray();

        // 缓冲区操作方法
        // 释放缓冲区 - 根据已确认的消息ID释放不再需要的输出缓冲区
        // writer: 调用此方法的写入器
        // maxId: 最大已确认消息ID，小于此ID的缓冲区可以释放
        void releaseBuffers(Writer* writer, uint64_t maxId);
        
        // 等待写入器工作 - 当构建器暂时没有消息需要处理时，进入休眠状态以等待写入器处理现有消息
        // writer: 相关联的写入器
        // writerQueueSize: 写入器队列当前大小
        // usec: 休眠微秒数
        void sleepForWriterWork(Writer* writer, uint64_t writerQueueSize, uint64_t usec);
        
        // 数据处理接口
        virtual void discard() = 0;
        virtual void commit() = 0;
        virtual void write(Scn scn, uint64_t messageIdx, bool redo, bool checkpoint) = 0;
        
        // 从事务对象创建消息
        virtual void checkPoint(TransactionChunk* transactionChunk) = 0;
        virtual void appendBegin(TransactionChunk* transactionChunk) = 0;
        virtual void appendCommit(TransactionChunk* transactionChunk) = 0;
        virtual void appendRollback(TransactionChunk* transactionChunk) = 0;
        virtual void appendDdl(TransactionChunk* transactionChunk) = 0;
        virtual void appendInsert(TransactionChunk* transactionChunk, uint64_t bid, typeRowId rowId, const SchemaElement* schemaElement, 
                               typeScn scn, const std::vector<Column*>& columns) = 0;
        virtual void appendDelete(TransactionChunk* transactionChunk, uint64_t bid, typeRowId rowId, const SchemaElement* schemaElement,
                               typeScn scn, const std::vector<Column*>& columns) = 0;
        virtual void appendUpdate(TransactionChunk* transactionChunk, uint64_t bid, typeRowId rowId, const SchemaElement* schemaElement,
                               typeScn scn, const std::vector<Column*>& columns, const std::vector<Column*>& columnsOld) = 0;
        
        // 消息处理计数
        [[nodiscard]] uint64_t getMessagesConfirmedTotal() const;
        virtual void resetCounters();
    };
}

#endif
