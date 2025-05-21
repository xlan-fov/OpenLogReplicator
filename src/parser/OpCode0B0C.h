/* Redo Log OP Code 11.12
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

#include "../common/RedoLogRecord.h"
#include "OpCode.h"

#ifndef OP_CODE_0B_0C_H_
#define OP_CODE_0B_0C_H_

namespace OpenLogReplicator {
    /**
     * 操作码0B0C处理类
     * 
     * 此类用于处理Oracle重做日志中的操作码0B0C，这个操作码通常表示多行删除操作。
     * 操作码0B0C常见于批量删除、表截断或索引重建等场景。
     */
    class OpCode0B0C final : public OpCode {
    public:
        /**
         * 处理操作码0B0C的方法
         * 
         * 此方法解析并处理重做日志记录中的0B0C操作码数据。主要处理以下步骤：
         * 1. 处理基本操作码信息
         * 2. 读取并处理KTB REDO信息(事务块重做)
         * 3. 读取并处理KDO操作码(内核数据操作)
         * 4. 如果配置了日志转储，会记录槽位信息
         * 
         * @param ctx 上下文对象，提供访问全局设置和功能
         * @param redoLogRecord 要处理的重做日志记录，包含操作码和相关数据
         */
        static void process0B0C(const Ctx* ctx, RedoLogRecord* redoLogRecord) {
            OpCode::process(ctx, redoLogRecord);
            typePos fieldPos = 0;
            typeField fieldNum = 0;
            typeSize fieldSize = 0;

            // 读取并处理第1个字段(KTB REDO信息)
            RedoLogRecord::nextField(ctx, redoLogRecord, fieldNum, fieldPos, fieldSize, 0x0B0C01);
            ktbRedo(ctx, redoLogRecord, fieldPos, fieldSize);

            // 读取并处理第2个字段(KDO操作码)，如果存在
            if (!RedoLogRecord::nextFieldOpt(ctx, redoLogRecord, fieldNum, fieldPos, fieldSize, 0x0B0C02))
                return;
            kdoOpCode(ctx, redoLogRecord, fieldPos, fieldSize);

            // 如果启用了日志转储，处理QMD(索引快速维护删除)操作信息
            if (unlikely(ctx->dumpRedoLog >= 1)) {
                if ((redoLogRecord->op & 0x1F) == RedoLogRecord::OP_QMD) {
                    // 输出每个要删除的槽位信息
                    for (typeCC i = 0; i < redoLogRecord->nRow; ++i)
                        *ctx->dumpStream << "slot[" << static_cast<uint>(i) << "]: " << std::dec <<
                                         ctx->read16(redoLogRecord->data(redoLogRecord->slotsDelta + (i * 2))) << '\n';
                }
            }
        }
    };
}

#endif
