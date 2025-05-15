/* 写入器基类实现
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

#include <algorithm>
#include <thread>
#include <utility>
#include <unistd.h>

#include "../builder/Builder.h"
#include "../common/Ctx.h"
#include "../common/exception/DataException.h"
#include "../common/exception/NetworkException.h"
#include "../common/exception/RuntimeException.h"
#include "../common/metrics/Metrics.h"
#include "../metadata/Metadata.h"
#include "Writer.h"

namespace OpenLogReplicator {
    Writer::Writer(Ctx* newCtx, std::string newAlias, std::string newDatabase, Builder* newBuilder, Metadata* newMetadata) :
            Thread(newCtx, std::move(newAlias)),
            database(std::move(newDatabase)),
            builder(newBuilder),
            metadata(newMetadata),
            checkpointTime(time(nullptr)) {
        ctx->writerThread = this;
    }

    Writer::~Writer() {
        delete[] queue;
        queue = nullptr;
    }

    void Writer::initialize() {
        if (queue != nullptr)
            return;
        queue = new BuilderMsg* [ctx->queueSize];
    }

    void Writer::createMessage(BuilderMsg* msg) {
        ++sentMessages;

        queue[currentQueueSize++] = msg;
        hwmQueueSize = std::max(currentQueueSize, hwmQueueSize);
    }

    void Writer::sortQueue() {
        if (currentQueueSize == 0)
            return;

        BuilderMsg** oldQueue = queue;
        queue = new BuilderMsg* [ctx->queueSize];
        uint64_t oldQueueSize = currentQueueSize;

        for (uint64_t newId = 0; newId < currentQueueSize; ++newId) {
            queue[newId] = oldQueue[0];
            uint64_t i = 0;
            --oldQueueSize;
            while (i < oldQueueSize) {
                if (i * 2 + 2 < oldQueueSize && oldQueue[(i * 2) + 2]->id < oldQueue[oldQueueSize]->id) {
                    if (oldQueue[(i * 2) + 1]->id < oldQueue[(i * 2) + 2]->id) {
                        oldQueue[i] = oldQueue[(i * 2) + 1];
                        i = i * 2 + 1;
                    } else {
                        oldQueue[i] = oldQueue[(i * 2) + 2];
                        i = i * 2 + 2;
                    }
                } else if (i * 2 + 1 < oldQueueSize && oldQueue[(i * 2) + 1]->id < oldQueue[oldQueueSize]->id) {
                    oldQueue[i] = oldQueue[(i * 2) + 1];
                    i = i * 2 + 1;
                } else
                    break;
            }
            oldQueue[i] = oldQueue[oldQueueSize];
        }

        delete[] oldQueue;
    }

    void Writer::resetMessageQueue() {
        for (uint64_t i = 0; i < currentQueueSize; ++i) {
            BuilderMsg* msg = queue[i];
            if (msg->isFlagSet(BuilderMsg::OUTPUT_BUFFER::ALLOCATED))
                delete[] msg->data;
        }
        currentQueueSize = 0;

        oldSize = builderQueue->start;
    }

    void Writer::confirmMessage(BuilderMsg* msg) {
        if (unlikely(ctx->isTraceSet(Ctx::TRACE::WRITER)))
            ctx->logTrace(Ctx::TRACE::WRITER, "confirmMessage: scn: " + msg->lwnScn.toString() + ", idx: " + std::to_string(msg->lwnIdx));

        // 更新确认的SCN和IDX
        if (msg->lwnScn > confirmedScn || (msg->lwnScn == confirmedScn && msg->lwnIdx > confirmedIdx)) {
            std::unique_lock<std::mutex> const lck(mtx);
            if (msg->lwnScn > confirmedScn || (msg->lwnScn == confirmedScn && msg->lwnIdx > confirmedIdx)) {
                confirmedScn = msg->lwnScn;
                confirmedIdx = msg->lwnIdx;

                // 更新检查点信息
                checkpointScn = msg->lwnScn;
                checkpointIdx = msg->lwnIdx;
                checkpointTime = time(nullptr);
            }
        }

        // 减少消息引用计数并释放内存
        if (msg->decRef() == 0) {
            if (msg->msgInd != nullptr) {
                ctx->freeMemoryChunk(this, Ctx::MEMORY::WRITER, msg->msgInd);
                msg->msgInd = nullptr;
            }
            if (msg->data != nullptr) {
                builder->freeChunk(this, msg);
                msg->data = nullptr;
            }
            delete msg;
        }

        // 清理队列中的已确认消息
        if (currentQueueSize > 0) {
            std::unique_lock<std::mutex> const lck(mtx);
            if (currentQueueSize > 0) {
                uint64_t i = 0;
                while (i < currentQueueSize && (queue[i]->lwnScn < confirmedScn || 
                        (queue[i]->lwnScn == confirmedScn && queue[i]->lwnIdx <= confirmedIdx))) {
                    ++i;
                }

                if (i > 0) {
                    // 保持未确认消息在队列开头
                    memmove(queue, queue + i, (currentQueueSize - i) * sizeof(BuilderMsg*));
                    currentQueueSize -= i;
                }
            }
        }
    }

    void Writer::run() {
        // 如果设置了线程跟踪，记录线程ID信息
        if (unlikely(ctx->isTraceSet(Ctx::TRACE::THREADS))) {
            std::ostringstream ss;
            ss << std::this_thread::get_id();
            ctx->logTrace(Ctx::TRACE::THREADS, "writer (" + ss.str() + ") start");
        }

        // 输出启动信息
        ctx->info(0, "writer is starting with " + getName());

        try {
            // 首先读取最新的检查点信息
            readCheckpoint();
            builderQueue = builder->firstBuilderQueue;
            oldSize = 0;
            currentQueueSize = 0;

            // 客户端连接的外部循环
            while (!ctx->hardShutdown) {
                try {
                    // 主处理循环
                    mainLoop();

                    // 客户端断开连接时的处理
                } catch (NetworkException& ex) {
                    ctx->warning(ex.code, ex.msg);
                    streaming = false;
                }

                if (ctx->softShutdown && ctx->replicatorFinished)
                    break;
            }
        } catch (DataException& ex) {
            ctx->error(ex.code, ex.msg);
            ctx->stopHard();
        } catch (RuntimeException& ex) {
            ctx->error(ex.code, ex.msg);
            ctx->stopHard();
        }

        // 输出停止信息
        ctx->info(0, "writer is stopping: " + getType() + ", hwm queue size: " + std::to_string(hwmQueueSize));
        if (unlikely(ctx->isTraceSet(Ctx::TRACE::THREADS))) {
            std::ostringstream ss;
            ss << std::this_thread::get_id();
            ctx->logTrace(Ctx::TRACE::THREADS, "writer (" + ss.str() + ") stop");
        }
    }

    void Writer::mainLoop() {
        // 设置运行状态并初始化消息队列
        contextSet(CONTEXT::CPU);
        oldSize = 0;
        queue = new BuilderMsg*[ctx->queueSize];
        currentQueueSize = 0;
        BuilderQueue* localBuilderQueue = builderQueue;

        // 主处理循环
        while (!ctx->hardShutdown) {
            // 处理消息队列
            pollQueue();

            // 检查新消息
            if (localBuilderQueue != nullptr && oldSize < localBuilderQueue->currentSize) {
                for (uint64_t pos = oldSize; pos < localBuilderQueue->currentSize; ++pos) {
                    createMessage(localBuilderQueue->msgs[pos]);
                }
                oldSize = localBuilderQueue->currentSize;
            }

            // 检查队列切换
            if (localBuilderQueue != nullptr && localBuilderQueue->next != nullptr) {
                localBuilderQueue = localBuilderQueue->next;
                oldSize = 0;
            }

            // 检查是否需要写入检查点
            if (sentMessages >= ctx->checkpointIntervalMb) {
                writeCheckpoint(false);
                sentMessages = 0;
            }

            // 没有新消息或客户端暂时不可用时，暂停一会
            if ((localBuilderQueue == nullptr || oldSize == localBuilderQueue->currentSize) && !ctx->softShutdown) {
                contextSet(CONTEXT::SLEEP);
                usleep(ctx->pollIntervalUs);
                contextSet(CONTEXT::CPU);
            }
        }

        // 确保写入最终检查点
        if (checkpointScn != Scn::none())
            writeCheckpoint(true);

        // 清理资源
        delete[] queue;
        queue = nullptr;
    }

    void Writer::writeCheckpoint(bool force) {
        redo = false;
        // Nothing changed
        if ((checkpointScn == confirmedScn && checkpointIdx == confirmedIdx) || confirmedScn == Scn::none())
            return;

        // Force first checkpoint
        if (checkpointScn == Scn::none())
            force = true;

        // Not yet
        const time_t now = time(nullptr);
        const uint64_t timeSinceCheckpoint = (now - checkpointTime);
        if (timeSinceCheckpoint < ctx->checkpointIntervalS && !force)
            return;

        if (unlikely(ctx->isTraceSet(Ctx::TRACE::CHECKPOINT))) {
            if (checkpointScn == Scn::none())
                ctx->logTrace(Ctx::TRACE::CHECKPOINT, "writer confirmed scn: " + confirmedScn.toString() + " idx: " +
                                                      std::to_string(confirmedIdx));
            else
                ctx->logTrace(Ctx::TRACE::CHECKPOINT, "writer confirmed scn: " + confirmedScn.toString() + " idx: " +
                                                      std::to_string(confirmedIdx) + " checkpoint scn: " + checkpointScn.toString() + " idx: " +
                                                      std::to_string(checkpointIdx));
        }
        const std::string name(database + "-chkpt");
        std::ostringstream ss;
        ss << R"({"database":")" << database
           << R"(","scn":)" << std::dec << confirmedScn.toString()
           << R"(,"idx":)" << std::dec << confirmedIdx
           << R"(,"resetlogs":)" << std::dec << metadata->resetlogs
           << R"(,"activation":)" << std::dec << metadata->activation << "}";

        if (metadata->stateWrite(name, confirmedScn, ss)) {
            checkpointScn = confirmedScn;
            checkpointIdx = confirmedIdx;
            checkpointTime = now;
        }
    }

    void Writer::readCheckpoint() {
        const std::string name(database + "-chkpt");

        // Checkpoint is present - read it
        std::string checkpoint;
        rapidjson::Document document;
        if (!metadata->stateRead(name, CHECKPOINT_FILE_MAX_SIZE, checkpoint))
            return;

        if (unlikely(checkpoint.empty() || document.Parse(checkpoint.c_str()).HasParseError()))
            throw DataException(20001, "file: " + name + " offset: " + std::to_string(document.GetErrorOffset()) +
                                       " - parse error: " + GetParseError_En(document.GetParseError()));

        if (!metadata->ctx->isDisableChecksSet(Ctx::DISABLE_CHECKS::JSON_TAGS)) {
            static const std::vector<std::string> documentNames {"database", "resetlogs", "activation", "scn", "idx"};
            Ctx::checkJsonFields(name, document, documentNames);
        }

        const std::string databaseJson = Ctx::getJsonFieldS(name, Ctx::JSON_PARAMETER_LENGTH, document, "database");
        if (unlikely(database != databaseJson))
            throw DataException(20001, "file: " + name + " - invalid database name: " + databaseJson);

        metadata->setResetlogs(Ctx::getJsonFieldU32(name, document, "resetlogs"));
        metadata->setActivation(Ctx::getJsonFieldU32(name, document, "activation"));

        // Started earlier - continue work and ignore default startup parameters
        checkpointScn = Ctx::getJsonFieldU64(name, document, "scn");
        metadata->clientScn = checkpointScn;
        if (document.HasMember("idx"))
            checkpointIdx = Ctx::getJsonFieldU64(name, document, "idx");
        else
            checkpointIdx = 0;
        metadata->clientIdx = checkpointIdx;
        metadata->startScn = checkpointScn;
        metadata->startSequence = Seq::none();
        metadata->startTime.clear();
        metadata->startTimeRel = 0;

        ctx->info(0, "checkpoint - all confirmed till scn: " + checkpointScn.toString() + ", idx: " +
                     std::to_string(checkpointIdx));
        metadata->setStatusReplicate(this);
    }

    void Writer::wakeUp() {
        contextSet(CONTEXT::WAIT_NOTIFY);
        wakeThreads();
        contextSet(CONTEXT::CPU);
    }
}
