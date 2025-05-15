/* Class with main redo log parser
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
#include <utility>

#include "../builder/Builder.h"
#include "../common/Clock.h"
#include "../common/DbLob.h"
#include "../common/DbTable.h"
#include "../common/XmlCtx.h"
#include "../common/exception/RedoLogException.h"
#include "../common/metrics/Metrics.h"
#include "../metadata/Metadata.h"
#include "../metadata/Schema.h"
#include "../reader/Reader.h"
#include "OpCode0501.h"
#include "OpCode0502.h"
#include "OpCode0504.h"
#include "OpCode0506.h"
#include "OpCode050B.h"
#include "OpCode0513.h"
#include "OpCode0514.h"
#include "OpCode0A02.h"
#include "OpCode0A08.h"
#include "OpCode0A12.h"
#include "OpCode0B02.h"
#include "OpCode0B03.h"
#include "OpCode0B04.h"
#include "OpCode0B05.h"
#include "OpCode0B06.h"
#include "OpCode0B08.h"
#include "OpCode0B0B.h"
#include "OpCode0B0C.h"
#include "OpCode0B10.h"
#include "OpCode0B16.h"
#include "OpCode1301.h"
#include "OpCode1801.h"
#include "OpCode1A02.h"
#include "OpCode1A06.h"
#include "Parser.h"
#include "Transaction.h"
#include "TransactionBuffer.h"

namespace OpenLogReplicator {
    // 解析器类的构造函数，初始化解析器所需的组件和内存
    Parser::Parser(Ctx* newCtx, Builder* newBuilder, Metadata* newMetadata, TransactionBuffer* newTransactionBuffer, int newGroup,
                   std::string newPath) :
            ctx(newCtx),
            builder(newBuilder),
            metadata(newMetadata),
            transactionBuffer(newTransactionBuffer),
            group(newGroup),
            path(std::move(newPath)) {

        // 初始化零值记录
        memset(reinterpret_cast<void*>(&zero), 0, sizeof(RedoLogRecord));

        // 为LWN(Log Writer Network)分配内存块
        lwnChunks[0] = ctx->getMemoryChunk(ctx->parserThread, Ctx::MEMORY::PARSER);
        ctx->parserThread->contextSet(Thread::CONTEXT::CPU);
        auto* size = reinterpret_cast<uint64_t*>(lwnChunks[0]);
        *size = sizeof(uint64_t);
        lwnAllocated = 1;
        lwnAllocatedMax = 1;
        lwnMembers[0] = nullptr;
    }

    // 解析器析构函数，释放所有分配的LWN内存块
    Parser::~Parser() {
        while (lwnAllocated > 0) {
            ctx->freeMemoryChunk(ctx->parserThread, Ctx::MEMORY::PARSER, lwnChunks[--lwnAllocated]);
        }
    }

    // 释放LWN内存块，但保留第一个
    void Parser::freeLwn() {
        while (lwnAllocated > 1) {
            ctx->freeMemoryChunk(ctx->parserThread, Ctx::MEMORY::PARSER, lwnChunks[--lwnAllocated]);
        }

        auto* size = reinterpret_cast<uint64_t*>(lwnChunks[0]);
        *size = sizeof(uint64_t);
    }

    // 分析LWN成员，这是解析重做日志的核心方法
    void Parser::analyzeLwn(LwnMember* lwnMember) {
        // 如果启用了LWN追踪，记录分析信息
        if (unlikely(ctx->isTraceSet(Ctx::TRACE::LWN)))
            ctx->logTrace(Ctx::TRACE::LWN, "analyze blk: " + std::to_string(lwnMember->block) + " offset: " +
                                           std::to_string(lwnMember->pageOffset) + " scn: " + lwnMember->scn.toString() + " subscn: " +
                                           std::to_string(lwnMember->subScn));

        // 获取数据指针
        uint8_t* data = reinterpret_cast<uint8_t*>(lwnMember) + sizeof(struct LwnMember);
        // 创建重做日志记录数组
        RedoLogRecord redoLogRecord[2];
        // ...后续实现解析逻辑
    }

    Reader::REDO_CODE Parser::parse() {
        typeBlk lwnConfirmedBlock = 2;
        uint64_t lwnRecords = 0;

        if (firstScn == Scn::none() && nextScn == Scn::none() && reader->getFirstScn() != Scn::zero()) {
            firstScn = reader->getFirstScn();
            nextScn = reader->getNextScn();
        }
        ctx->suppLogSize = 0;

        if (reader->getBufferStart() == FileOffset(2, reader->getBlockSize())) {
            if (unlikely(ctx->dumpRedoLog >= 1)) {
                const std::string fileName = ctx->dumpPath + "/" + sequence.toString() + ".olr";
                ctx->dumpStream->open(fileName);
                if (!ctx->dumpStream->is_open()) {
                    ctx->error(10006, "file: " + fileName + " - open for writing returned: " + strerror(errno));
                    ctx->warning(60012, "aborting log dump");
                    ctx->dumpRedoLog = 0;
                }
                std::ostringstream ss;
                reader->printHeaderInfo(ss, path);
                *ctx->dumpStream << ss.str();
            }
        }

        // Continue started offset
        if (metadata->fileOffset > FileOffset::zero()) {
            if (unlikely(!metadata->fileOffset.matchesBlockSize(reader->getBlockSize())))
                throw RedoLogException(50047, "incorrect offset start: " + metadata->fileOffset.toString() +
                                              " - not a multiplication of block size: " + std::to_string(reader->getBlockSize()));

            lwnConfirmedBlock = metadata->fileOffset.getBlock(reader->getBlockSize());
            if (unlikely(ctx->isTraceSet(Ctx::TRACE::CHECKPOINT)))
                ctx->logTrace(Ctx::TRACE::CHECKPOINT, "setting reader start position to " + metadata->fileOffset.toString() + " (block " +
                                                      std::to_string(lwnConfirmedBlock) + ")");
            metadata->fileOffset = FileOffset::zero();
        }
        reader->setBufferStartEnd(FileOffset(lwnConfirmedBlock, reader->getBlockSize()),
                                  FileOffset(lwnConfirmedBlock, reader->getBlockSize()));

        ctx->info(0, "processing redo log: " + toString() + " offset: " + reader->getBufferStart().toString());
        if (ctx->isFlagSet(Ctx::REDO_FLAGS::ADAPTIVE_SCHEMA) && !metadata->schema->loaded && !ctx->versionStr.empty()) {
            metadata->loadAdaptiveSchema();
            metadata->schema->loaded = true;
        }

        if (metadata->resetlogs == 0)
            metadata->setResetlogs(reader->getResetlogs());

        if (unlikely(metadata->resetlogs != reader->getResetlogs()))
            throw RedoLogException(50048, "invalid resetlogs value (found: " + std::to_string(reader->getResetlogs()) + ", expected: " +
                                          std::to_string(metadata->resetlogs) + "): " + reader->fileName);

        if (reader->getActivation() != 0 && (metadata->activation == 0 || metadata->activation != reader->getActivation())) {
            ctx->info(0, "new activation detected: " + std::to_string(reader->getActivation()));
            metadata->setActivation(reader->getActivation());
        }

        const time_ut cStart = ctx->clock->getTimeUt();
        reader->setStatusRead();
        LwnMember* lwnMember;
        uint16_t blockOffset;
        FileOffset confirmedBufferStart = reader->getBufferStart();
        uint64_t recordPos = 0;
        uint32_t recordSize4;
        uint32_t recordLeftToCopy = 0;
        const typeBlk startBlock = lwnConfirmedBlock;
        typeBlk currentBlock = lwnConfirmedBlock;
        typeBlk lwnEndBlock = lwnConfirmedBlock;
        typeLwn lwnNumMax = 0;
        typeLwn lwnNumCnt = 0;
        lwnCheckpointBlock = lwnConfirmedBlock;
        bool switchRedo = false;

        while (!ctx->softShutdown) {
            // There is some work to do
            while (confirmedBufferStart < reader->getBufferEnd()) {
                uint64_t redoBufferPos = (static_cast<uint64_t>(currentBlock) * reader->getBlockSize()) % Ctx::MEMORY_CHUNK_SIZE;
                const uint64_t redoBufferNum =
                        ((static_cast<uint64_t>(currentBlock) * reader->getBlockSize()) / Ctx::MEMORY_CHUNK_SIZE) % ctx->memoryChunksReadBufferMax;
                const uint8_t* redoBlock = reader->redoBufferList[redoBufferNum] + redoBufferPos;

                blockOffset = 16U;
                // New LWN block
                if (currentBlock == lwnEndBlock) {
                    const uint8_t vld = redoBlock[blockOffset + 4U];

                    if (likely((vld & 0x04) != 0)) {
                        const uint16_t lwnNum = ctx->read16(redoBlock + blockOffset + 24U);
                        const uint32_t lwnSize = ctx->read32(redoBlock + blockOffset + 28U);
                        lwnEndBlock = currentBlock + lwnSize;
                        lwnScn = ctx->readScn(redoBlock + blockOffset + 40U);
                        lwnTimestamp = ctx->read32(redoBlock + blockOffset + 64U);

                        if (ctx->metrics != nullptr) {
                            const int64_t diff = ctx->clock->getTimeT() - lwnTimestamp.toEpoch(ctx->hostTimezone);
                            ctx->metrics->emitCheckpointLag(diff);
                        }

                        if (lwnNumCnt == 0) {
                            lwnCheckpointBlock = currentBlock;
                            lwnNumMax = ctx->read16(redoBlock + blockOffset + 26U);
                            // Verify LWN header start
                            if (unlikely(lwnScn < reader->getFirstScn() || (lwnScn > reader->getNextScn() && reader->getNextScn() != Scn::none())))
                                throw RedoLogException(50049, "invalid lwn scn: " + lwnScn.toString());
                        } else {
                            const typeLwn lwnNumCur = ctx->read16(redoBlock + blockOffset + 26U);
                            if (unlikely(lwnNumCur != lwnNumMax))
                                throw RedoLogException(50050, "invalid lwn max: " + std::to_string(lwnNum) + "/" +
                                                              std::to_string(lwnNumCur) + "/" + std::to_string(lwnNumMax));
                        }
                        ++lwnNumCnt;

                        if (unlikely(ctx->isTraceSet(Ctx::TRACE::LWN))) {
                            const typeBlk lwnStartBlock = currentBlock;
                            ctx->logTrace(Ctx::TRACE::LWN, "at: " + std::to_string(lwnStartBlock) + " size: " + std::to_string(lwnSize) +
                                                           " chk: " + std::to_string(lwnNum) + " max: " + std::to_string(lwnNumMax));
                        }
                    } else
                        throw RedoLogException(50051, "did not find lwn at offset: " + confirmedBufferStart.toString());
                }

                while (blockOffset < reader->getBlockSize()) {
                    // Next record
                    if (recordLeftToCopy == 0) {
                        if (blockOffset + 20U >= reader->getBlockSize())
                            break;

                        recordSize4 = (static_cast<uint64_t>(ctx->read32(redoBlock + blockOffset)) + 3U) & 0xFFFFFFFC;
                        if (recordSize4 > 0) {
                            auto* recordSize = reinterpret_cast<uint64_t*>(lwnChunks[lwnAllocated - 1]);

                            if (((*recordSize + sizeof(struct LwnMember) + recordSize4 + 7) & 0xFFFFFFF8) > Ctx::MEMORY_CHUNK_SIZE_MB * 1024 * 1024) {
                                if (unlikely(lwnAllocated == MAX_LWN_CHUNKS))
                                    throw RedoLogException(50052, "all " + std::to_string(MAX_LWN_CHUNKS) + " lwn buffers allocated");

                                lwnChunks[lwnAllocated++] = ctx->getMemoryChunk(ctx->parserThread, Ctx::MEMORY::PARSER);
                                ctx->parserThread->contextSet(Thread::CONTEXT::CPU);
                                lwnAllocatedMax = std::max(lwnAllocated, lwnAllocatedMax);
                                recordSize = reinterpret_cast<uint64_t*>(lwnChunks[lwnAllocated - 1]);
                                *recordSize = sizeof(uint64_t);
                            }

                            if (unlikely(((*recordSize + sizeof(struct LwnMember) + recordSize4 + 7) & 0xFFFFFFF8) > Ctx::MEMORY_CHUNK_SIZE_MB * 1024 * 1024))
                                throw RedoLogException(50053, "too big redo log record, size: " + std::to_string(recordSize4));

                            lwnMember = reinterpret_cast<struct LwnMember*>(lwnChunks[lwnAllocated - 1] + *recordSize);
                            *recordSize += (sizeof(struct LwnMember) + recordSize4 + 7) & 0xFFFFFFF8;
                            lwnMember->pageOffset = blockOffset;
                            lwnMember->scn = ctx->read32(redoBlock + blockOffset + 8U) |
                                             (static_cast<uint64_t>(ctx->read16(redoBlock + blockOffset + 6U)) << 32);
                            lwnMember->size = recordSize4;
                            lwnMember->block = currentBlock;
                            lwnMember->subScn = ctx->read16(redoBlock + blockOffset + 12U);

                            if (unlikely(ctx->isTraceSet(Ctx::TRACE::LWN)))
                                ctx->logTrace(Ctx::TRACE::LWN, "size: " + std::to_string(recordSize4) + " scn: " +
                                                               lwnMember->scn.toString() + " subscn: " + std::to_string(lwnMember->subScn));

                            uint64_t lwnPos = ++lwnRecords;
                            if (unlikely(lwnPos >= MAX_RECORDS_IN_LWN))
                                throw RedoLogException(50054, "all " + std::to_string(lwnPos) + " records in lwn were used");

                            while (lwnPos > 1 && *lwnMember < *lwnMembers[lwnPos / 2]) {
                                lwnMembers[lwnPos] = lwnMembers[lwnPos / 2];
                                lwnPos /= 2;
                            }
                            lwnMembers[lwnPos] = lwnMember;
                        }

                        recordLeftToCopy = recordSize4;
                        recordPos = 0;
                    }

                    // Nothing more
                    if (recordLeftToCopy == 0)
                        break;

                    uint32_t toCopy;
                    if (blockOffset + recordLeftToCopy > reader->getBlockSize())
                        toCopy = reader->getBlockSize() - blockOffset;
                    else
                        toCopy = recordLeftToCopy;

                    memcpy(reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(lwnMember) + sizeof(struct LwnMember) + recordPos),
                           reinterpret_cast<const void*>(redoBlock + blockOffset), toCopy);
                    recordLeftToCopy -= toCopy;
                    blockOffset += toCopy;
                    recordPos += toCopy;
                }

                ++currentBlock;
                confirmedBufferStart += reader->getBlockSize();
                redoBufferPos += reader->getBlockSize();

                // Checkpoint
                if (unlikely(ctx->isTraceSet(Ctx::TRACE::LWN)))
                    ctx->logTrace(Ctx::TRACE::LWN, "checkpoint at " + std::to_string(currentBlock) + "/" + std::to_string(lwnEndBlock) +
                                                   " num: " + std::to_string(lwnNumCnt) + "/" + std::to_string(lwnNumMax));
                if (currentBlock == lwnEndBlock && lwnNumCnt == lwnNumMax) {
                    lastTransaction = nullptr;

                    if (unlikely(ctx->isTraceSet(Ctx::TRACE::LWN)))
                        ctx->logTrace(Ctx::TRACE::LWN, "* analyze: " + lwnScn.toString());

                    while (lwnRecords > 0) {
                        try {
                            analyzeLwn(lwnMembers[1]);
                        } catch (DataException& ex) {
                            if (ctx->isFlagSet(Ctx::REDO_FLAGS::IGNORE_DATA_ERRORS)) {
                                ctx->error(ex.code, ex.msg);
                                ctx->warning(60013, "forced to continue working in spite of error");
                            } else
                                throw DataException(ex.code, "runtime error, aborting further redo log processing: " + ex.msg);
                        } catch (RedoLogException& ex) {
                            if (ctx->isFlagSet(Ctx::REDO_FLAGS::IGNORE_DATA_ERRORS)) {
                                ctx->error(ex.code, ex.msg);
                                ctx->warning(60013, "forced to continue working in spite of error");
                            } else
                                throw RedoLogException(ex.code, "runtime error, aborting further redo log processing: " + ex.msg);
                        }

                        if (lwnRecords == 1) {
                            lwnRecords = 0;
                            break;
                        }

                        uint64_t lwnPos = 1;
                        while (true) {
                            if (lwnPos * 2U < lwnRecords && *lwnMembers[lwnPos * 2U] < *lwnMembers[lwnRecords]) {
                                if ((lwnPos * 2U) + 1U < lwnRecords && *lwnMembers[(lwnPos * 2U) + 1U] < *lwnMembers[lwnPos * 2U]) {
                                    lwnMembers[lwnPos] = lwnMembers[(lwnPos * 2U) + 1U];
                                    lwnPos *= 2U;
                                    ++lwnPos;
                                } else {
                                    lwnMembers[lwnPos] = lwnMembers[lwnPos * 2U];
                                    lwnPos *= 2U;
                                }
                            } else if ((lwnPos * 2U) + 1U < lwnRecords && *lwnMembers[(lwnPos * 2U) + 1U] < *lwnMembers[lwnRecords]) {
                                lwnMembers[lwnPos] = lwnMembers[(lwnPos * 2U) + 1U];
                                lwnPos *= 2U;
                                ++lwnPos;
                            } else
                                break;
                        }

                        lwnMembers[lwnPos] = lwnMembers[lwnRecords];
                        --lwnRecords;
                    }

                    if (lwnScn > metadata->firstDataScn) {
                        if (unlikely(ctx->isTraceSet(Ctx::TRACE::CHECKPOINT)))
                            ctx->logTrace(Ctx::TRACE::CHECKPOINT, "on: " + lwnScn.toString());
                        builder->processCheckpoint(lwnScn, sequence, lwnTimestamp.toEpoch(ctx->hostTimezone),
                                                   FileOffset(currentBlock, reader->getBlockSize()), switchRedo);

                        Seq minSequence = Seq::none();
                        FileOffset minFileOffset;
                        Xid minXid;
                        transactionBuffer->checkpoint(minSequence, minFileOffset, minXid);
                        if (unlikely(ctx->isTraceSet(Ctx::TRACE::LWN)))
                            ctx->logTrace(Ctx::TRACE::LWN, "* checkpoint: " + lwnScn.toString());
                        metadata->checkpoint(ctx->parserThread, lwnScn, lwnTimestamp, sequence, FileOffset(currentBlock, reader->getBlockSize()),
                                             static_cast<uint64_t>(currentBlock - lwnConfirmedBlock) * reader->getBlockSize(), minSequence,
                                             minFileOffset, minXid);

                        if (ctx->stopCheckpoints > 0 && metadata->isNewData(lwnScn, builder->lwnIdx)) {
                            --ctx->stopCheckpoints;
                            if (ctx->stopCheckpoints == 0) {
                                ctx->info(0, "shutdown started - exhausted number of checkpoints");
                                ctx->stopSoft();
                            }
                        }
                        if (ctx->metrics != nullptr)
                            ctx->metrics->emitCheckpointsOut(1);
                    } else {
                        if (ctx->metrics != nullptr)
                            ctx->metrics->emitCheckpointsSkip(1);
                    }

                    lwnNumCnt = 0;
                    freeLwn();

                    if (ctx->metrics != nullptr)
                        ctx->metrics->emitBytesParsed((currentBlock - lwnConfirmedBlock) * reader->getBlockSize());
                    lwnConfirmedBlock = currentBlock;
                } else if (unlikely(lwnNumCnt > lwnNumMax))
                    throw RedoLogException(50055, "lwn overflow: " + std::to_string(lwnNumCnt) + "/" + std::to_string(lwnNumMax));

                // Free memory
                if (redoBufferPos == Ctx::MEMORY_CHUNK_SIZE) {
                    reader->bufferFree(ctx->parserThread, redoBufferNum);
                    reader->confirmReadData(confirmedBufferStart);
                }
            }

            // Processing finished
            if (!switchRedo && lwnScn > Scn::zero() && confirmedBufferStart == reader->getBufferEnd() && reader->getRet() == Reader::REDO_CODE::FINISHED) {
                if (lwnScn > metadata->firstDataScn) {
                    switchRedo = true;
                    if (unlikely(ctx->isTraceSet(Ctx::TRACE::CHECKPOINT)))
                        ctx->logTrace(Ctx::TRACE::CHECKPOINT, "on: " + lwnScn.toString() + " with switch");
                    builder->processCheckpoint(lwnScn, sequence, lwnTimestamp.toEpoch(ctx->hostTimezone),
                                               FileOffset(currentBlock, reader->getBlockSize()), switchRedo);
                    if (ctx->metrics != nullptr)
                        ctx->metrics->emitCheckpointsOut(1);
                } else {
                    if (ctx->metrics != nullptr)
                        ctx->metrics->emitCheckpointsSkip(1);
                }
            }

            if (ctx->softShutdown) {
                if (unlikely(ctx->isTraceSet(Ctx::TRACE::CHECKPOINT)))
                    ctx->logTrace(Ctx::TRACE::CHECKPOINT, "on: " + lwnScn.toString() + " at exit");
                builder->processCheckpoint(lwnScn, sequence, lwnTimestamp.toEpoch(ctx->hostTimezone),
                                           FileOffset(currentBlock, reader->getBlockSize()), false);
                if (ctx->metrics != nullptr)
                    ctx->metrics->emitCheckpointsOut(1);

                reader->setRet(Reader::REDO_CODE::SHUTDOWN);
            } else {
                if (reader->checkFinished(ctx->parserThread, confirmedBufferStart)) {
                    if (reader->getRet() == Reader::REDO_CODE::FINISHED && nextScn == Scn::none() && reader->getNextScn() != Scn::none())
                        nextScn = reader->getNextScn();
                    if (reader->getRet() == Reader::REDO_CODE::STOPPED || reader->getRet() == Reader::REDO_CODE::OVERWRITTEN)
                        metadata->fileOffset = FileOffset(lwnConfirmedBlock, reader->getBlockSize());
                    break;
                }
            }
        }

        if (ctx->metrics != nullptr && reader->getNextScn() != Scn::none()) {
            const int64_t diff = ctx->clock->getTimeT() - reader->getNextTime().toEpoch(ctx->hostTimezone);

            if (group == 0) {
                ctx->metrics->emitLogSwitchesArchived(1);
                ctx->metrics->emitLogSwitchesLagArchived(diff);
            } else {
                ctx->metrics->emitLogSwitchesOnline(1);
                ctx->metrics->emitLogSwitchesLagOnline(diff);
            }
        }

        // Print performance information
        if (ctx->isTraceSet(Ctx::TRACE::PERFORMANCE)) {
            const time_ut cEnd = ctx->clock->getTimeUt();
            double suppLogPercent = 0.0;
            if (currentBlock != startBlock)
                suppLogPercent = 100.0 * ctx->suppLogSize / (static_cast<double>(currentBlock - startBlock) * reader->getBlockSize());

            if (group == 0) {
                double mySpeed = 0;
                const double myTime = static_cast<double>(cEnd - cStart) / 1000.0;
                if (myTime > 0)
                    mySpeed = static_cast<double>(currentBlock - startBlock) * reader->getBlockSize() * 1000.0 / 1024 / 1024 / myTime;

                double myReadSpeed = 0;
                if (reader->getSumTime() > 0)
                    myReadSpeed = (static_cast<double>(reader->getSumRead()) * 1000000.0 / 1024 / 1024 / static_cast<double>(reader->getSumTime()));

                ctx->logTrace(Ctx::TRACE::PERFORMANCE, std::to_string(myTime) + " ms, " +
                                                       "Speed: " + std::to_string(mySpeed) + " MB/s, " +
                                                       "Redo log size: " + std::to_string(static_cast<uint64_t>(currentBlock - startBlock) *
                                                                                          reader->getBlockSize() / 1024 / 1024) +
                                                       " MB, Read size: " + std::to_string(reader->getSumRead() / 1024 / 1024) + " MB, " +
                                                       "Read speed: " + std::to_string(myReadSpeed) + " MB/s, " +
                                                       "Max LWN size: " + std::to_string(lwnAllocatedMax) + ", " +
                                                       "Supplemental redo log size: " + std::to_string(ctx->suppLogSize) + " bytes " +
                                                       "(" + std::to_string(suppLogPercent) + " %)");
            } else {
                ctx->logTrace(Ctx::TRACE::PERFORMANCE, "Redo log size: " + std::to_string(static_cast<uint64_t>(currentBlock - startBlock) *
                                                                                          reader->getBlockSize() / 1024 / 1024) + " MB, " +
                                                       "Max LWN size: " + std::to_string(lwnAllocatedMax) + ", " +
                                                       "Supplemental redo log size: " + std::to_string(ctx->suppLogSize) + " bytes " +
                                                       "(" + std::to_string(suppLogPercent) + " %)");
            }
        }

        if (ctx->dumpRedoLog >= 1 && ctx->dumpStream->is_open()) {
            *ctx->dumpStream << "END OF REDO DUMP\n";
            ctx->dumpStream->close();
        }

        builder->flush();
        freeLwn();
        return reader->getRet();
    }

    std::string Parser::toString() const {
        return "group: " + std::to_string(group) + " scn: " + firstScn.toString() + " to " +
               (nextScn != Scn::none() ? nextScn.toString() : "0") + " seq: " + sequence.toString() + " path: " + path;
    }
}
