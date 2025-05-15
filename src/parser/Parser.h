/* Header for Parser class
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

#ifndef PARSER_H_
#define PARSER_H_

#include <cstddef>

#include "../common/Ctx.h"
#include "../common/RedoLogRecord.h"
#include "../reader/Reader.h"
#include "../common/types/Time.h"
#include "../common/types/Types.h"
#include "../common/types/Xid.h"

namespace OpenLogReplicator {
    class Builder;
    class Metadata;
    class Transaction;
    class TransactionBuffer;
    class XmlCtx;

    // LWN成员结构体，表示日志写入网络中的一个成员
    struct LwnMember {
        uint16_t pageOffset;  // 页面偏移量
        Scn scn;              // 系统变更号
        uint32_t size;        // 大小
        typeBlk block;        // 块号
        typeSubScn subScn;    // 子SCN

        // 比较操作符，用于排序
        bool operator<(const LwnMember& other) const {
            return (pageOffset < other.pageOffset);
        }
    };

    // 解析器类，用于解析重做日志数据
    class Parser final {
    protected:
        // 常量定义
        static constexpr uint64_t MAX_LWN_CHUNKS = static_cast<uint64_t>(512 * 2) / Ctx::MEMORY_CHUNK_SIZE_MB;  // 最大LWN块数
        static constexpr uint64_t MAX_RECORDS_IN_LWN = 1048576;  // LWN中最大记录数

        // 基础组件
        Ctx* ctx;                      // 上下文
        Builder* builder;              // 构建器
        Metadata* metadata;            // 元数据
        TransactionBuffer* transactionBuffer;  // 事务缓冲区
        RedoLogRecord zero;            // 零记录
        Transaction* lastTransaction{nullptr};  // 最后处理的事务

        // LWN内存管理
        uint8_t* lwnChunks[MAX_LWN_CHUNKS]{};  // LWN块数组
        LwnMember* lwnMembers[MAX_RECORDS_IN_LWN + 1]{};  // LWN成员数组
        uint64_t lwnAllocated{0};      // 已分配的LWN块数
        uint64_t lwnAllocatedMax{0};   // 最大分配的LWN块数
        Time lwnTimestamp{0};          // LWN时间戳
        Scn lwnScn;                    // LWN SCN
        typeBlk lwnCheckpointBlock{0}; // LWN检查点块

        // 私有方法
        void freeLwn();   // 释放LWN内存
        void analyzeLwn(LwnMember* lwnMember);  // 分析LWN成员
        
        // 事务操作相关方法
        void appendToTransactionDdl(RedoLogRecord* redoLogRecord1);  // 添加DDL操作到事务
        void appendToTransactionBegin(RedoLogRecord* redoLogRecord1);  // 添加事务开始操作
        void appendToTransactionCommit(RedoLogRecord* redoLogRecord1);  // 添加事务提交操作
        void appendToTransactionLob(RedoLogRecord* redoLogRecord1);  // 添加LOB操作到事务
        void appendToTransactionIndex(RedoLogRecord* redoLogRecord1, RedoLogRecord* redoLogRecord2);  // 添加索引操作到事务
        void appendToTransaction(RedoLogRecord* redoLogRecord1);  // 添加普通操作到事务
        void appendToTransactionRollback(RedoLogRecord* redoLogRecord1);  // 添加回滚操作到事务
        void appendToTransaction(RedoLogRecord* redoLogRecord1, RedoLogRecord* redoLogRecord2);  // 添加双记录操作到事务
        void appendToTransactionRollback(RedoLogRecord* redoLogRecord1, RedoLogRecord* redoLogRecord2);  // 添加双记录回滚操作到事务
        void dumpRedoVector(const uint8_t* data, typeSize recordSize) const;  // 转储重做向量

    public:
        // 公共属性
        int group;                  // 组号
        std::string path;           // 路径
        Seq sequence;               // 序列号
        Scn firstScn{Scn::none()};  // 首个SCN
        Scn nextScn{Scn::none()};   // 下一个SCN
        Reader* reader{nullptr};    // 读取器

        // 解析方法，返回处理结果
        Reader::REDO_CODE parse();
        
        // 返回解析器的字符串表示
        [[nodiscard]] std::string toString() const;
    };
}

#endif
