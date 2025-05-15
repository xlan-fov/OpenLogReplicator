/* 批处理模式下的日志复制线程实现
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

#include "../common/exception/RuntimeException.h"
#include "../metadata/Metadata.h"
#include "../metadata/Schema.h"
#include "ReplicatorBatch.h"

namespace OpenLogReplicator {
    // 构造函数 - 初始化批处理复制器
    ReplicatorBatch::ReplicatorBatch(Ctx* newCtx, void (* newArchGetLog)(Replicator* replicator), Builder* newBuilder, Metadata* newMetadata,
                                     TransactionBuffer* newTransactionBuffer, std::string newAlias, std::string newDatabase) :
            Replicator(newCtx, newArchGetLog, newBuilder, newMetadata, newTransactionBuffer, std::move(newAlias), std::move(newDatabase)) {
    }

    // 定位读取器 - 设置起始位置
    void ReplicatorBatch::positionReader() {
        // 如果指定了起始序列号，从该序列号开始
        if (metadata->startSequence != Seq::none())
            metadata->setSeqFileOffset(metadata->startSequence, FileOffset::zero());
        else
            metadata->setSeqFileOffset(Seq::zero(), FileOffset::zero());
        metadata->sequence = 0;
    }

    // 创建架构 - 批处理模式需要预先存在架构信息
    void ReplicatorBatch::createSchema() {
        if (ctx->isFlagSet(Ctx::REDO_FLAGS::SCHEMALESS))
            return;

        // 批处理模式需要已存在的架构文件
        ctx->hint("if you don't have earlier schema, try with schemaless mode ('flags': 2)");
        if (metadata->schema->scn != Scn::none())
            ctx->hint("you can also set start SCN for writer: 'start-scn': " + metadata->schema->scn.toString());

        throw RuntimeException(10052, "schema file missing");
    }

    // 更新在线重做日志数据 - 批处理模式不需要此操作
    void ReplicatorBatch::updateOnlineRedoLogData() {
        // 批处理模式无需更新在线重做日志数据
    }

    // 获取模式名称
    std::string ReplicatorBatch::getModeName() const {
        return {"batch"};
    }

    // 批处理完成后的操作 - 退出程序
    bool ReplicatorBatch::continueWithOnline() {
        ctx->info(0, "批处理完成，正在退出");  // 中文化：已将消息从"finished batch processing, exiting"翻译为中文
        ctx->stopSoft();
        return false;
    }
}
