/* Base class for process which is reading from redo log files
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

#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

#include "../common/Clock.h"
#include "../common/Ctx.h"
#include "../common/RedoLogRecord.h"
#include "../common/exception/RuntimeException.h"
#include "../common/metrics/Metrics.h"
#include "../common/types/Seq.h"
#include "Reader.h"

/*
 * Reader类实现 - 处理Oracle重做日志文件的读取
 * 
 * 该类负责从文件系统读取Oracle重做日志，验证其有效性，
 * 并将数据提供给Parser进行进一步处理。
 *
 * 主要功能：
 * 1. 打开和读取重做日志文件
 * 2. 验证文件头和块数据的有效性
 * 3. 管理内存缓冲区以高效读取和处理数据
 * 4. 在实时模式下处理日志切换
 * 5. 可选地复制读取的日志到归档位置
 */
namespace OpenLogReplicator {
    const char* Reader::REDO_MSG[]{"OK", "OVERWRITTEN", "FINISHED", "STOPPED", "SHUTDOWN", "EMPTY", "READ ERROR",
                                   "WRITE ERROR", "SEQUENCE ERROR", "CRC ERROR", "BLOCK ERROR", "BAD DATA ERROR",
                                   "OTHER ERROR"};

    Reader::Reader(Ctx* newCtx, std::string newAlias, std::string newDatabase, int newGroup, bool newConfiguredBlockSum) :
            Thread(newCtx, std::move(newAlias)),
            database(std::move(newDatabase)),
            configuredBlockSum(newConfiguredBlockSum),
            group(newGroup) {
    }

    void Reader::initialize() {
        // 如果配置了重做日志复制路径，确保目录可读
        if (!ctx->redoCopyPath.empty()) {
            if (opendir(ctx->redoCopyPath.c_str()) == nullptr)
                throw RuntimeException(10012, "directory: " + ctx->redoCopyPath + " - can't read");
        }
    }

    void Reader::wakeUp() {
        // 设置当前上下文为互斥操作，原因是唤醒读取器
        contextSet(CONTEXT::MUTEX, REASON::READER_WAKE_UP);
        {
            // 获取互斥锁并通知所有等待条件的线程
            std::unique_lock<std::mutex> const lck(mtx);
            condBufferFull.notify_all();
            condReaderSleeping.notify_all();
            condParserSleeping.notify_all();
        }
        // 恢复CPU上下文
        contextSet(CONTEXT::CPU);
    }

    Reader::~Reader() {
        // 释放所有读取缓冲区
        for (uint num = 0; num < ctx->memoryChunksReadBufferMax; ++num)
            bufferFree(this, num);

        // 释放缓冲区列表
        delete[] redoBufferList;
        redoBufferList = nullptr;

        // 释放头部缓冲区
        if (headerBuffer != nullptr) {
            free(headerBuffer);
            headerBuffer = nullptr;
        }

        // 处理文件复制描述符
        if (fileCopyDes != -1) {
            close(fileCopyDes);
            fileCopyDes = -1;
        }
    }

    Reader::REDO_CODE Reader::checkBlockHeader(uint8_t* buffer, typeBlk blockNumber, bool showHint) {
        if (buffer[0] == 0 && buffer[1] == 0)
            return REDO_CODE::EMPTY;

        if ((blockSize == 512 && buffer[1] != 0x22) ||
            (blockSize == 1024 && buffer[1] != 0x22) ||
            (blockSize == 4096 && buffer[1] != 0x82)) {
            ctx->error(40001, "file: " + fileName + " - block: " + std::to_string(blockNumber) + " - invalid block size: " +
                              std::to_string(blockSize) + ", header[1]: " + std::to_string(static_cast<uint>(buffer[1])));
            return REDO_CODE::ERROR_BAD_DATA;
        }

        const typeBlk blockNumberHeader = ctx->read32(buffer + 4);
        const Seq sequenceHeader = Seq(ctx->read32(buffer + 8));

        if (sequence == Seq::zero() || status == STATUS::UPDATE) {
            sequence = sequenceHeader;
        } else {
            if (group == 0) {
                if (sequence != sequenceHeader) {
                    ctx->warning(60024, "file: " + fileName + " - invalid header sequence, found: " + sequenceHeader.toString() +
                                        ", expected: " + sequence.toString());
                    return REDO_CODE::ERROR_SEQUENCE;
                }
            } else {
                if (sequence > sequenceHeader)
                    return REDO_CODE::EMPTY;
                if (sequence < sequenceHeader)
                    return REDO_CODE::OVERWRITTEN;
            }
        }

        if (blockNumberHeader != blockNumber) {
            ctx->error(40002, "file: " + fileName + " - invalid header block number: " + std::to_string(blockNumberHeader) +
                              ", expected: " + std::to_string(blockNumber));
            return REDO_CODE::ERROR_BLOCK;
        }

        if (!ctx->isDisableChecksSet(Ctx::DISABLE_CHECKS::BLOCK_SUM)) {
            const typeSum chSum = ctx->read16(buffer + 14);
            const typeSum chSumCalculated = calcChSum(buffer, blockSize);
            if (chSum != chSumCalculated) {
                if (showHint) {
                    ctx->warning(60025, "file: " + fileName + " - block: " + std::to_string(blockNumber) +
                                        " - invalid header checksum, expected: " + std::to_string(chSum) + ", calculated: " +
                                        std::to_string(chSumCalculated));
                    if (!hintDisplayed) {
                        if (!configuredBlockSum) {
                            ctx->hint("set DB_BLOCK_CHECKSUM = TYPICAL on the database or turn off consistency checking in OpenLogReplicator"
                                      " setting parameter disable-checks: " + std::to_string(static_cast<int>(Ctx::DISABLE_CHECKS::BLOCK_SUM)) +
                                      " for the reader");
                        }
                        hintDisplayed = true;
                    }
                }
                return REDO_CODE::ERROR_CRC;
            }
        }

        return REDO_CODE::OK;
    }

    /**
     * 读取数据块大小
     * 
     * @param prevRead 上一次读取的大小
     * @return 应该读取的大小
     */
    uint Reader::readSize(uint prevRead) {
        if (prevRead < blockSize)
            return blockSize;

        prevRead *= 2;
        prevRead = std::min<uint64_t>(prevRead, Ctx::MEMORY_CHUNK_SIZE);

        return prevRead;
    }

    /**
     * 重新读取并验证重做日志头信息
     * 
     * @return 读取操作的结果码
     */
    Reader::REDO_CODE Reader::reloadHeaderRead() {
        // 如果系统正在关闭，返回错误
        if (ctx->softShutdown)
            return REDO_CODE::ERROR;

        // 读取日志文件头部
        int actualRead = redoRead(headerBuffer, 0, blockSize > 0 ? blockSize * 2 : PAGE_SIZE_MAX * 2);
        if (actualRead < Ctx::MIN_BLOCK_SIZE) {
            ctx->error(40003, "file: " + fileName + " - " + strerror(errno));
            return REDO_CODE::ERROR_READ;
        }
        
        // 如果有度量收集器，记录读取的字节数
        if (ctx->metrics != nullptr)
            ctx->metrics->emitBytesRead(actualRead);

        // 检查文件头标记
        if (headerBuffer[0] != 0) {
            ctx->error(40003, "file: " + fileName + " - invalid header[0]: " + std::to_string(static_cast<uint>(headerBuffer[0])));
            return REDO_CODE::ERROR_BAD_DATA;
        }

        // 检查大小端标记(字节序)
        if (headerBuffer[28] == 0x7A && headerBuffer[29] == 0x7B && headerBuffer[30] == 0x7C && headerBuffer[31] == 0x7D) {
            // 大端序标记，如果当前设置为小端序则切换
            if (!ctx->isBigEndian())
                ctx->setBigEndian();
        } else if (headerBuffer[28] != 0x7D || headerBuffer[29] != 0x7C || headerBuffer[30] != 0x7B || headerBuffer[31] != 0x7A || ctx->isBigEndian()) {
            // 无效的字节序标记
            ctx->error(40004, "file: " + fileName + " - invalid header[28-31]: " + std::to_string(static_cast<uint>(headerBuffer[28])) +
                              ", " + std::to_string(static_cast<uint>(headerBuffer[29])) + ", " + std::to_string(static_cast<uint>(headerBuffer[30])) +
                              ", " + std::to_string(static_cast<uint>(headerBuffer[31])));
            return REDO_CODE::ERROR_BAD_DATA;
        }

        // 验证块大小
        bool blockSizeOK = false;
        blockSize = ctx->read32(headerBuffer + 20);
        if ((blockSize == 512 && headerBuffer[1] == 0x22) || 
            (blockSize == 1024 && headerBuffer[1] == 0x22) || 
            (blockSize == 4096 && headerBuffer[1] == 0x82))
            blockSizeOK = true;

        if (!blockSizeOK) {
            ctx->error(40005, "file: " + fileName + " - invalid block size: " + std::to_string(blockSize) + ", header[1]: " +
                              std::to_string(static_cast<uint>(headerBuffer[1])));
            blockSize = 0;
            return REDO_CODE::ERROR_BAD_DATA;
        }

        // 确保读取了足够的数据
        if (actualRead < static_cast<int>(blockSize * 2)) {
            ctx->error(40003, "file: " + fileName + " - " + strerror(errno));
            return REDO_CODE::ERROR_READ;
        }

        // 如果需要复制日志，将头部信息写入到复制文件
        if (!ctx->redoCopyPath.empty()) {
            // 确保只复制需要的数据
            if (static_cast<uint>(actualRead) > blockSize * 2)
                actualRead = static_cast<int>(blockSize * 2);

            // 获取序列号并处理文件复制
            const Seq sequenceHeader = Seq(ctx->read32(headerBuffer + blockSize + 8));
            if (fileCopySequence != sequenceHeader) {
                if (fileCopyDes != -1) {
                    close(fileCopyDes);
                    fileCopyDes = -1;
                }
            }

            // 创建新的复制文件
            if (fileCopyDes == -1) {
                fileNameWrite = ctx->redoCopyPath + "/" + database + "_" + sequenceHeader.toString() + ".arc";
                fileCopyDes = open(fileNameWrite.c_str(), O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
                if (unlikely(fileCopyDes == -1))
                    throw RuntimeException(10006, "file: " + fileNameWrite + " - open for writing returned: " + strerror(errno));
                ctx->info(0, "writing redo log copy to: " + fileNameWrite);
                fileCopySequence = sequenceHeader;
            }

            // 写入数据到复制文件
            const int bytesWritten = pwrite(fileCopyDes, headerBuffer, actualRead, 0);
            if (bytesWritten != actualRead) {
                ctx->error(10007, "file: " + fileNameWrite + " - " + std::to_string(bytesWritten) + " bytes written instead of " +
                                  std::to_string(actualRead) + ", code returned: " + strerror(errno));
                return REDO_CODE::ERROR_WRITE;
            }
        }

        return REDO_CODE::OK;
    }

    /**
     * 重新加载并验证重做日志头信息，包括版本检查
     * 
     * @return 操作结果码
     */
    Reader::REDO_CODE Reader::reloadHeader() {
        // 读取并验证日志头
        REDO_CODE retReload = reloadHeaderRead();
        if (retReload != REDO_CODE::OK)
            return retReload;

        // 检查数据库版本兼容性
        uint32_t version;
        compatVsn = ctx->read32(headerBuffer + blockSize + 20);
        if (compatVsn == 0)
            return REDO_CODE::EMPTY;

        // 验证支持的Oracle版本
        if ((compatVsn >= 0x0B200000 && compatVsn <= 0x0B200400)        // 11.2.0.0 - 11.2.0.4
            || (compatVsn >= 0x0C100000 && compatVsn <= 0x0C100200)     // 12.1.0.0 - 12.1.0.2
            || (compatVsn >= 0x0C200000 && compatVsn <= 0x0C200100)     // 12.2.0.0 - 12.2.0.1
            || (compatVsn >= 0x12000000 && compatVsn <= 0x120E0000)     // 18.0.0.0 - 18.14.0.0
            || (compatVsn >= 0x13000000 && compatVsn <= 0x13120000)     // 19.0.0.0 - 19.18.0.0
            || (compatVsn >= 0x15000000 && compatVsn <= 0x15080000)     // 21.0.0.0 - 21.8.0.0
            || (compatVsn >= 0x17000000 && compatVsn <= 0x17030000))    // 23.0.0.0 - 23.3.0.0
            version = compatVsn;
        else {
            ctx->error(40006, "file: " + fileName + " - invalid database version: " + std::to_string(compatVsn));
            return REDO_CODE::ERROR_BAD_DATA;
        }

        // 读取头部关键信息
        activation = ctx->read32(headerBuffer + blockSize + 52);     // 激活ID
        numBlocksHeader = ctx->read32(headerBuffer + blockSize + 156); // 块数量
        resetlogs = ctx->read32(headerBuffer + blockSize + 160);    // 重置日志计数
        firstScnHeader = ctx->readScn(headerBuffer + blockSize + 180); // 首个SCN
        firstTimeHeader = ctx->read32(headerBuffer + blockSize + 188); // 首个时间
        nextScnHeader = ctx->readScn(headerBuffer + blockSize + 192); // 下一个SCN
        nextTime = ctx->read32(headerBuffer + blockSize + 200);     // 下一个时间

        // 根据头部信息更新文件大小
        if (numBlocksHeader != Ctx::ZERO_BLK && fileSize > static_cast<uint64_t>(numBlocksHeader) * blockSize && group == 0) {
            fileSize = static_cast<uint64_t>(numBlocksHeader) * blockSize;
            ctx->info(0, "updating redo log size to: " + std::to_string(fileSize) + " for: " + fileName);
        }

        // 如果是首次读取，初始化版本信息
        if (ctx->version == 0) {
            // 读取SID (数据库系统标识符)
            char SID[9];
            memcpy(reinterpret_cast<void*>(SID),
                   reinterpret_cast<const void*>(headerBuffer + blockSize + 28), 8);
            SID[8] = 0;
            ctx->version = version;
            
            // 根据Oracle版本设置列限制
            if (compatVsn >= RedoLogRecord::REDO_VERSION_23_0)
                ctx->columnLimit = Ctx::COLUMN_LIMIT_23_0;
                
            // 构造版本字符串
            const Seq sequenceHeader = Seq(ctx->read32(headerBuffer + blockSize + 8));

            if (compatVsn < RedoLogRecord::REDO_VERSION_18_0) {
                ctx->versionStr = std::to_string(compatVsn >> 24) + "." + std::to_string((compatVsn >> 20) & 0xF) + "." +
                                  std::to_string((compatVsn >> 16) & 0xF) + "." + std::to_string((compatVsn >> 8) & 0xFF);
            } else {
                ctx->versionStr = std::to_string(compatVsn >> 24) + "." + std::to_string((compatVsn >> 16) & 0xFF) + "." +
                                  std::to_string((compatVsn >> 8) & 0xFF);
            }
            
            // 记录找到的重做日志版本信息
            ctx->info(0, "found redo log version: " + ctx->versionStr + ", activation: " + std::to_string(activation) + ", resetlogs: " +
                         std::to_string(resetlogs) + ", page: " + std::to_string(blockSize) + ", sequence: " + sequenceHeader.toString() +
                         ", SID: " + SID + ", endian: " + (ctx->isBigEndian() ? "BIG" : "LITTLE"));
        }

        // 确保版本一致性
        if (version != ctx->version) {
            ctx->error(40007, "file: " + fileName + " - invalid database version: " + std::to_string(compatVsn) + ", expected: " +
                              std::to_string(ctx->version));
            return REDO_CODE::ERROR_BAD_DATA;
        }

        // 验证第一个数据块的CRC校验
        uint badBlockCrcCount = 0;
        retReload = checkBlockHeader(headerBuffer + blockSize, 1, false);
        if (unlikely(ctx->isTraceSet(Ctx::TRACE::DISK)))
            ctx->logTrace(Ctx::TRACE::DISK, "block: 1 check: " + std::to_string(static_cast<uint>(retReload)));

        // 如果CRC校验失败，多次重试
        while (retReload == REDO_CODE::ERROR_CRC) {
            ++badBlockCrcCount;
            if (badBlockCrcCount == BAD_CDC_MAX_CNT)
                return REDO_CODE::ERROR_BAD_DATA;

            // 等待一小段时间再重试
            contextSet(CONTEXT::SLEEP);
            usleep(ctx->redoReadSleepUs);
            contextSet(CONTEXT::CPU);
            retReload = checkBlockHeader(headerBuffer + blockSize, 1, false);
            if (unlikely(ctx->isTraceSet(Ctx::TRACE::DISK)))
                ctx->logTrace(Ctx::TRACE::DISK, "block: 1 check: " + std::to_string(static_cast<uint>(retReload)));
        }

        if (retReload != REDO_CODE::OK)
            return retReload;

        // 更新SCN (系统变更号)信息
        if (firstScn == Scn::none() || status == STATUS::UPDATE) {
            firstScn = firstScnHeader;
            nextScn = nextScnHeader;
        } else {
            // 验证SCN一致性
            if (firstScnHeader != firstScn) {
                ctx->error(40008, "file: " + fileName + " - invalid first scn value: " + firstScnHeader.toString() + ", expected: " + firstScn.toString());
                return REDO_CODE::ERROR_BAD_DATA;
            }
        }

        // 更新下一个SCN
        if (nextScn == Scn::none() && nextScnHeader != Scn::none()) {
            if (unlikely(ctx->isTraceSet(Ctx::TRACE::DISK)))
                ctx->logTrace(Ctx::TRACE::DISK, "updating next scn to: " + nextScnHeader.toString());
            nextScn = nextScnHeader;
        } else if (nextScn != Scn::none() && nextScnHeader != Scn::none() && nextScn != nextScnHeader) {
            ctx->error(40009, "file: " + fileName + " - invalid next scn value: " + nextScnHeader.toString() + ", expected: " +
                              nextScn.toString());
            return REDO_CODE::ERROR_BAD_DATA;
        }

        return retReload;
    }

    /**
     * 从重做日志文件读取数据块 (第一次读取方式)
     * 主要用于初始读取或在没有缓存的情况下读取
     * 
     * @return 读取操作是否成功
     */
    bool Reader::read1() {
        // 确定要读取的字节数
        uint toRead = readSize(lastRead);

        // 确保不会超出文件范围
        if (bufferScan + toRead > fileSize)
            toRead = fileSize - bufferScan;

        // 计算目标缓冲区位置
        const uint64_t redoBufferPos = bufferScan % Ctx::MEMORY_CHUNK_SIZE;
        const uint64_t redoBufferNum = (bufferScan / Ctx::MEMORY_CHUNK_SIZE) % ctx->memoryChunksReadBufferMax;
        
        // 确保读取不会超出内存块边界
        if (redoBufferPos + toRead > Ctx::MEMORY_CHUNK_SIZE)
            toRead = Ctx::MEMORY_CHUNK_SIZE - redoBufferPos;

        // 检查是否有数据可读
        if (toRead == 0) {
            ctx->error(40010, "file: " + fileName + " - zero to read, start: " + std::to_string(bufferStart) + ", end: " +
                              std::to_string(bufferEnd) + ", scan: " + std::to_string(bufferScan));
            ret = REDO_CODE::ERROR;
            return false;
        }

        // 分配内存缓冲区
        bufferAllocate(redoBufferNum);
        
        // 记录读取操作细节
        if (unlikely(ctx->isTraceSet(Ctx::TRACE::DISK)))
            ctx->logTrace(Ctx::TRACE::DISK, "reading#1 " + fileName + " at (" + std::to_string(bufferStart) + "/" +
                                            std::to_string(bufferEnd) + "/" + std::to_string(bufferScan) + ") bytes: " + std::to_string(toRead));
        
        // 执行实际的读取操作
        const int actualRead = redoRead(redoBufferList[redoBufferNum] + redoBufferPos, bufferScan, toRead);

        // 记录读取结果
        if (unlikely(ctx->isTraceSet(Ctx::TRACE::DISK)))
            ctx->logTrace(Ctx::TRACE::DISK, "reading#1 " + fileName + " at (" + std::to_string(bufferStart) + "/" +
                                            std::to_string(bufferEnd) + "/" + std::to_string(bufferScan) + ") got: " + std::to_string(actualRead));
        
        // 处理读取错误
        if (actualRead < 0) {
            ctx->error(40003, "file: " + fileName + " - " + strerror(errno));
            ret = REDO_CODE::ERROR_READ;
            return false;
        }
        
        // 记录读取的字节数
        if (ctx->metrics != nullptr)
            ctx->metrics->emitBytesRead(actualRead);

        // 如果需要，将读取的数据复制到备份文件
        if (actualRead > 0 && fileCopyDes != -1 && (ctx->redoVerifyDelayUs == 0 || group == 0)) {
            const int bytesWritten = pwrite(fileCopyDes, redoBufferList[redoBufferNum] + redoBufferPos, actualRead,
                                            static_cast<int64_t>(bufferEnd));
            if (bytesWritten != actualRead) {
                ctx->error(10007, "file: " + fileNameWrite + " - " + std::to_string(bytesWritten) + " bytes written instead of " +
                                  std::to_string(actualRead) + ", code returned: " + strerror(errno));
                ret = REDO_CODE::ERROR_WRITE;
                return false;
            }
        }

        // 计算读取了多少完整的块
        const typeBlk maxNumBlock = actualRead / blockSize;
        const typeBlk bufferScanBlock = bufferScan / blockSize;
        uint goodBlocks = 0;
        REDO_CODE currentRet = REDO_CODE::OK;

        // 验证各个块的有效性
        for (typeBlk numBlock = 0; numBlock < maxNumBlock; ++numBlock) {
            currentRet = checkBlockHeader(redoBufferList[redoBufferNum] + redoBufferPos + (numBlock * blockSize), bufferScanBlock + numBlock,
                                          ctx->redoVerifyDelayUs == 0 || group == 0);
            if (unlikely(ctx->isTraceSet(Ctx::TRACE::DISK)))
                ctx->logTrace(Ctx::TRACE::DISK, "block: " + std::to_string(bufferScanBlock + numBlock) + " check: " +
                                                std::to_string(static_cast<uint>(currentRet)));

            if (currentRet != REDO_CODE::OK)
                break;
            ++goodBlocks;
        }

        // 处理部分填充的在线重做日志文件
        if (goodBlocks == 0 && group == 0) {
            if (nextScnHeader != Scn::none()) {
                ret = REDO_CODE::FINISHED;
                nextScn = nextScnHeader;
            } else {
                ctx->warning(60023, "file: " + fileName + " - position: " + std::to_string(bufferScan) + " - unexpected end of file");
                ret = REDO_CODE::STOPPED;
            }
            return false;
        }

        // 如果设置了延迟验证，将CRC错误视为空块
        if (currentRet == REDO_CODE::ERROR_CRC && ctx->redoVerifyDelayUs > 0 && group != 0)
            currentRet = REDO_CODE::EMPTY;

        // 处理错误情况
        if (goodBlocks == 0 && currentRet != REDO_CODE::OK && currentRet != REDO_CODE::EMPTY) {
            ret = currentRet;
            return false;
        }

        // 检查日志切换
        if (goodBlocks == 0 && currentRet == REDO_CODE::EMPTY) {
            currentRet = reloadHeader();
            if (currentRet != REDO_CODE::OK) {
                ret = currentRet;
                return false;
            }
            reachedZero = true;
        } else {
            readBlocks = true;
            reachedZero = false;
        }

        // 更新读取状态
        lastRead = goodBlocks * blockSize;
        lastReadTime = ctx->clock->getTimeUt();
        if (goodBlocks > 0) {
            // 对于延迟验证模式的处理
            if (ctx->redoVerifyDelayUs > 0 && group != 0) {
                bufferScan += goodBlocks * blockSize;

                // 记录读取时间戳以便后续验证
                for (uint numBlock = 0; numBlock < goodBlocks; ++numBlock) {
                    auto* readTimeP = reinterpret_cast<time_t*>(redoBufferList[redoBufferNum] + redoBufferPos + (numBlock * blockSize));
                    *readTimeP = lastReadTime;
                }
            } else {
                // 标准模式下更新缓冲区位置并通知解析器
                {
                    contextSet(CONTEXT::MUTEX, REASON::READER_READ1);
                    std::unique_lock<std::mutex> const lck(mtx);
                    bufferEnd += goodBlocks * blockSize;
                    bufferScan = bufferEnd;
                    condParserSleeping.notify_all();
                }
                contextSet(CONTEXT::CPU);
            }
        }

        // 批处理模式下处理部分填充的在线重做日志
        if (currentRet == REDO_CODE::ERROR_SEQUENCE && group == 0) {
            if (nextScnHeader != Scn::none()) {
                ret = REDO_CODE::FINISHED;
                nextScn = nextScnHeader;
            } else {
                ctx->warning(60023, "file: " + fileName + " - position: " + std::to_string(bufferScan) + " - unexpected end of file");
                ret = REDO_CODE::STOPPED;
            }
            return false;
        }

        return true;
    }

    /**
     * 从已扫描但未处理的数据中读取 (第二种读取方式)
     * 主要用于延迟验证模式下的读取
     * 
     * @return 读取操作是否成功
     */
    bool Reader::read2() {
        // 计算可能需要读取的块数量
        uint maxNumBlock = (bufferScan - bufferEnd) / blockSize;
        uint goodBlocks = 0;
        maxNumBlock = std::min<uint64_t>(maxNumBlock, Ctx::MEMORY_CHUNK_SIZE / blockSize);

        // 根据时间戳检查哪些块已经准备好处理
        for (uint numBlock = 0; numBlock < maxNumBlock; ++numBlock) {
            const uint64_t redoBufferPos = (bufferEnd + numBlock * blockSize) % Ctx::MEMORY_CHUNK_SIZE;
            const uint64_t redoBufferNum = ((bufferEnd + numBlock * blockSize) / Ctx::MEMORY_CHUNK_SIZE) % ctx->memoryChunksReadBufferMax;

            // 检查块是否已经等待了足够长的时间
            const auto* const readTimeP = reinterpret_cast<const time_ut*>(redoBufferList[redoBufferNum] + redoBufferPos);
            if (*readTimeP + static_cast<time_ut>(ctx->redoVerifyDelayUs) < loopTime) {
                ++goodBlocks;
            } else {
                readTime = *readTimeP + static_cast<time_t>(ctx->redoVerifyDelayUs);
                break;
            }
        }

        if (goodBlocks > 0) {
            // 计算需要读取的总字节数
            uint toRead = readSize(goodBlocks * blockSize);
            toRead = std::min(toRead, goodBlocks * blockSize);

            // 计算缓冲区位置
            const uint64_t redoBufferPos = bufferEnd % Ctx::MEMORY_CHUNK_SIZE;
            const uint64_t redoBufferNum = (bufferEnd / Ctx::MEMORY_CHUNK_SIZE) % ctx->memoryChunksReadBufferMax;

            // 确保不会超出内存块边界
            if (redoBufferPos + toRead > Ctx::MEMORY_CHUNK_SIZE)
                toRead = Ctx::MEMORY_CHUNK_SIZE - redoBufferPos;

            // 检查是否有数据可读
            if (toRead == 0) {
                ctx->error(40011, "zero to read (start: " + std::to_string(bufferStart) + ", end: " + std::to_string(bufferEnd) +
                                  ", scan: " + std::to_string(bufferScan) + "): " + fileName);
                ret = REDO_CODE::ERROR;
                return false;
            }

            // 记录读取操作
            if (unlikely(ctx->isTraceSet(Ctx::TRACE::DISK)))
                ctx->logTrace(Ctx::TRACE::DISK, "reading#2 " + fileName + " at (" + std::to_string(bufferStart) + "/" +
                                                std::to_string(bufferEnd) + "/" + std::to_string(bufferScan) + ") bytes: " + std::to_string(toRead));
            
            // 执行实际的读取操作
            const int actualRead = redoRead(redoBufferList[redoBufferNum] + redoBufferPos, bufferEnd, toRead);

            // 记录读取结果
            if (unlikely(ctx->isTraceSet(Ctx::TRACE::DISK)))
                ctx->logTrace(Ctx::TRACE::DISK, "reading#2 " + fileName + " at (" + std::to_string(bufferStart) + "/" +
                                                std::to_string(bufferEnd) + "/" + std::to_string(bufferScan) + ") got: " + std::to_string(actualRead));

            // 处理读取错误
            if (actualRead < 0) {
                ctx->error(40003, "file: " + fileName + " - " + strerror(errno));
                ret = REDO_CODE::ERROR_READ;
                return false;
            }
            
            // 记录读取的字节数
            if (ctx->metrics != nullptr)
                ctx->metrics->emitBytesRead(actualRead);

            // 如果需要，将读取的数据复制到备份文件
            if (actualRead > 0 && fileCopyDes != -1) {
                const int bytesWritten = pwrite(fileCopyDes, redoBufferList[redoBufferNum] + redoBufferPos, actualRead,
                                                static_cast<int64_t>(bufferEnd));
                if (bytesWritten != actualRead) {
                    ctx->error(10007, "file: " + fileNameWrite + " - " + std::to_string(bytesWritten) +
                                      " bytes written instead of " + std::to_string(actualRead) + ", code returned: " + strerror(errno));
                    ret = REDO_CODE::ERROR_WRITE;
                    return false;
                }
            }

            readBlocks = true;
            REDO_CODE currentRet = REDO_CODE::OK;
            maxNumBlock = actualRead / blockSize;
            const typeBlk bufferEndBlock = bufferEnd / blockSize;

            // 验证每个读取的块是否有效
            for (uint numBlock = 0; numBlock < maxNumBlock; ++numBlock) {
                currentRet = checkBlockHeader(redoBufferList[redoBufferNum] + redoBufferPos + (numBlock * blockSize),
                                              bufferEndBlock + numBlock, true);
                if (unlikely(ctx->isTraceSet(Ctx::TRACE::DISK)))
                    ctx->logTrace(Ctx::TRACE::DISK, "block: " + std::to_string(bufferEndBlock + numBlock) + " check: " +
                                                    std::to_string(static_cast<uint>(currentRet)));

                if (currentRet != REDO_CODE::OK)
                    break;
                ++goodBlocks;
            }

            // 在线重做日志需要定期验证头部
            if (currentRet == REDO_CODE::OK && group > 0)
                currentRet = reloadHeader();

            // 处理错误情况
            if (currentRet != REDO_CODE::OK) {
                ret = currentRet;
                return false;
            }

            // 更新缓冲区信息并通知解析器
            {
                contextSet(CONTEXT::MUTEX, REASON::READER_READ2);
                std::unique_lock<std::mutex> const lck(mtx);
                bufferEnd += actualRead;
                condParserSleeping.notify_all();
            }
            contextSet(CONTEXT::CPU);
        }

        return true;
    }

    /**
     * 主循环 - 处理读取操作并管理状态转换
     */
    void Reader::mainLoop() {
        while (!ctx->softShutdown) {
            {
                // 通知解析器并检查当前状态
                contextSet(CONTEXT::MUTEX, REASON::READER_MAIN1);
                std::unique_lock<std::mutex> lck(mtx);
                condParserSleeping.notify_all();

                // 如果状态是休眠，等待被唤醒
                if (status == STATUS::SLEEPING && !ctx->softShutdown) {
                    if (unlikely(ctx->isTraceSet(Ctx::TRACE::SLEEP)))
                        ctx->logTrace(Ctx::TRACE::SLEEP, "Reader:mainLoop:sleep");
                    contextSet(CONTEXT::WAIT, REASON::READER_NO_WORK);
                    condReaderSleeping.wait(lck);
                    contextSet(CONTEXT::MUTEX, REASON::READER_MAIN2);
                } else if (status == STATUS::READ && !ctx->softShutdown && (bufferEnd % Ctx::MEMORY_CHUNK_SIZE) == 0) {
                    // 缓冲区已满的警告
                    ctx->warning(0, "buffer full?");
                }
            }
            contextSet(CONTEXT::CPU);

            // 如果系统正在关闭，退出循环
            if (ctx->softShutdown)
                break;

            // 处理检查状态 - 打开文件
            if (status == STATUS::CHECK) {
                if (unlikely(ctx->isTraceSet(Ctx::TRACE::FILE)))
                    ctx->logTrace(Ctx::TRACE::FILE, "trying to open: " + fileName);
                redoClose();
                const REDO_CODE currentRet = redoOpen();
                {
                    contextSet(CONTEXT::MUTEX, REASON::READER_CHECK_STATUS);
                    std::unique_lock<std::mutex> const lck(mtx);
                    ret = currentRet;
                    status = STATUS::SLEEPING;
                    condParserSleeping.notify_all();
                }
                contextSet(CONTEXT::CPU);
                continue;
            }

            // 处理更新状态 - 重新加载头部信息
            if (status == STATUS::UPDATE) {
                if (fileCopyDes != -1) {
                    close(fileCopyDes);
                    fileCopyDes = -1;
                }

                sumRead = 0;
                sumTime = 0;
                // 重新加载日志头部
                const REDO_CODE currentRet = reloadHeader();
                if (currentRet == REDO_CODE::OK) {
                    bufferStart = blockSize * 2;
                    bufferEnd = blockSize * 2;
                }

                // 释放所有缓冲区
                for (uint num = 0; num < ctx->memoryChunksReadBufferMax; ++num)
                    bufferFree(this, num);

                // 更新状态并通知解析器
                {
                    contextSet(CONTEXT::MUTEX, REASON::READER_SLEEP1);
                    std::unique_lock<std::mutex> const lck(mtx);
                    ret = currentRet;
                    status = STATUS::SLEEPING;
                    condParserSleeping.notify_all();
                }
                contextSet(CONTEXT::CPU);
            } else if (status == STATUS::READ) {
                // 处理读取状态 - 执行实际的读取操作
                if (unlikely(ctx->isTraceSet(Ctx::TRACE::DISK)))
                    ctx->logTrace(Ctx::TRACE::DISK, "reading " + fileName + " at (" + std::to_string(bufferStart) + "/" +
                                                    std::to_string(bufferEnd) + ") at size: " + std::to_string(fileSize));
                
                // 初始化读取参数
                lastRead = blockSize;
                lastReadTime = 0;
                readTime = 0;
                bufferScan = bufferEnd;
                reachedZero = false;

                // 主要读取循环
                while (!ctx->softShutdown && status == STATUS::READ) {
                    loopTime = ctx->clock->getTimeUt();
                    readBlocks = false;
                    readTime = 0;

                    // 检查是否已到达文件末尾
                    if (bufferEnd == fileSize) {
                        if (nextScnHeader != Scn::none()) {
                            ret = REDO_CODE::FINISHED;
                            nextScn = nextScnHeader;
                        } else {
                            ctx->warning(60023, "file: " + fileName + " - position: " + std::to_string(bufferScan) +
                                                " - unexpected end of file");
                            ret = REDO_CODE::STOPPED;
                        }
                        break;
                    }

                    // 检查缓冲区是否已满
                    if (bufferStart + ctx->bufferSizeMax == bufferEnd) {
                        contextSet(CONTEXT::MUTEX, REASON::READER_FULL);
                        std::unique_lock<std::mutex> lck(mtx);
                        if (!ctx->softShutdown && bufferStart + ctx->bufferSizeMax == bufferEnd) {
                            if (unlikely(ctx->isTraceSet(Ctx::TRACE::SLEEP)))
                                ctx->logTrace(Ctx::TRACE::SLEEP, "Reader:mainLoop:bufferFull");
                            contextSet(CONTEXT::WAIT, REASON::READER_BUFFER_FULL);
                            condBufferFull.wait(lck);
                            contextSet(CONTEXT::CPU);
                            continue;
                        }
                    }

                    // 如果已扫描数据未被消费，执行第二种读取
                    if (bufferEnd < bufferScan)
                        if (!read2())
                            break;

                    // 执行第一种读取来获取新数据
                    if (bufferScan < fileSize && (bufferIsFree() || (bufferScan % Ctx::MEMORY_CHUNK_SIZE) > 0)
                        && (!reachedZero || lastReadTime + static_cast<time_t>(ctx->redoReadSleepUs) < loopTime))
                        if (!read1())
                            break;

                    // 检查是否已读取文件中所有块
                    if (numBlocksHeader != Ctx::ZERO_BLK && bufferEnd == static_cast<uint64_t>(numBlocksHeader) * blockSize) {
                        if (nextScnHeader != Scn::none()) {
                            ret = REDO_CODE::FINISHED;
                            nextScn = nextScnHeader;
                        } else {
                            ctx->warning(60023, "file: " + fileName + " - position: " + std::to_string(bufferScan) +
                                                " - unexpected end of file");
                            ret = REDO_CODE::STOPPED;
                        }
                        break;
                    }

                    // 如果没有读取到块，短暂休眠
                    if (!readBlocks) {
                        if (readTime == 0) {
                            contextSet(CONTEXT::SLEEP);
                            usleep(ctx->redoReadSleepUs);
                            contextSet(CONTEXT::CPU);
                        } else {
                            const time_ut nowTime = ctx->clock->getTimeUt();
                            if (readTime > nowTime) {
                                if (static_cast<time_ut>(ctx->redoReadSleepUs) < readTime - nowTime) {
                                    contextSet(CONTEXT::SLEEP);
                                    usleep(ctx->redoReadSleepUs);
                                    contextSet(CONTEXT::CPU);
                                } else {
                                    contextSet(CONTEXT::SLEEP);
                                    usleep(readTime - nowTime);
                                    contextSet(CONTEXT::CPU);
                                }
                            }
                        }
                    }
                }

                // 读取完成后，更新状态并通知解析器
                {
                    contextSet(CONTEXT::MUTEX, REASON::READER_SLEEP2);
                    std::unique_lock<std::mutex> const lck(mtx);
                    status = STATUS::SLEEPING;
                    condParserSleeping.notify_all();
                }
                contextSet(CONTEXT::CPU);
            }
        }
    }

    /**
     * 计算块的校验和
     * 
     * @param buffer 数据缓冲区
     * @param size 数据大小
     * @return 计算出的校验和
     */
    typeSum Reader::calcChSum(uint8_t* buffer, uint size) const {
        const typeSum oldChSum = ctx->read16(buffer + 14);
        uint64_t sum = 0;

        // 按8字节块计算XOR校验和
        for (uint i = 0; i < size / 8; ++i, buffer += sizeof(uint64_t))
            sum ^= *reinterpret_cast<const uint64_t*>(buffer);
        
        // 压缩到16位
        sum ^= (sum >> 32);
        sum ^= (sum >> 16);
        sum ^= oldChSum;

        return sum & 0xFFFF;
    }

    /**
     * 线程主入口
     * 初始化Reader并执行主循环，处理异常情况
     */
    void Reader::run() {
        // 记录线程启动
        if (unlikely(ctx->isTraceSet(Ctx::TRACE::THREADS))) {
            std::ostringstream ss;
            ss << std::this_thread::get_id();
            ctx->logTrace(Ctx::TRACE::THREADS, "reader (" + ss.str() + ") start");
        }

        try {
            // 执行主循环
            mainLoop();
        } catch (RuntimeException& ex) {
            // 处理运行时异常
            ctx->error(ex.code, ex.msg);
            ctx->stopHard();
        } catch (std::bad_alloc& ex) {
            // 处理内存分配失败
            ctx->error(10018, "memory allocation failed: " + std::string(ex.what()));
            ctx->stopHard();
        }

        // 清理资源
        redoClose();
        if (fileCopyDes != -1) {
            close(fileCopyDes);
            fileCopyDes = -1;
        }

        // 记录线程结束
        if (unlikely(ctx->isTraceSet(Ctx::TRACE::THREADS))) {
            std::ostringstream ss;
            ss << std::this_thread::get_id();
            ctx->logTrace(Ctx::TRACE::THREADS, "reader (" + ss.str() + ") stop");
        }
    }

    /**
     * 分配缓冲区内存块
     * 
     * @param num 内存块编号
     */
    void Reader::bufferAllocate(uint num) {
        // 检查内存块是否已分配
        {
            contextSet(CONTEXT::MUTEX, REASON::READER_ALLOCATE1);
            std::unique_lock<std::mutex> const lck(mtx);
            if (redoBufferList[num] != nullptr) {
                contextSet(CONTEXT::CPU);
                return;
            }
        }
        contextSet(CONTEXT::CPU);

        // 获取新的内存块
        uint8_t* buffer = ctx->getMemoryChunk(this, Ctx::MEMORY::READER);

        // 更新内存块列表
        {
            contextSet(CONTEXT::MUTEX, REASON::READER_ALLOCATE2);
            std::unique_lock<std::mutex> const lck(mtx);
            redoBufferList[num] = buffer;
            --ctx->bufferSizeFree;
        }
        contextSet(CONTEXT::CPU);
    }

    /**
     * 释放缓冲区内存块
     * 
     * @param t 当前线程
     * @param num 要释放的内存块编号
     */
    void Reader::bufferFree(Thread* t, uint num) {
        uint8_t* buffer;
        // 检查内存块是否已分配
        {
            t->contextSet(CONTEXT::MUTEX, REASON::READER_FREE);
            std::unique_lock<std::mutex> const lck(mtx);
            if (redoBufferList[num] == nullptr) {
                t->contextSet(CONTEXT::CPU);
                return;
            }
            buffer = redoBufferList[num];
            redoBufferList[num] = nullptr;
            ++ctx->bufferSizeFree;
        }
        t->contextSet(CONTEXT::CPU);

        // 将内存块返回到内存池
        ctx->freeMemoryChunk(this, Ctx::MEMORY::READER, buffer);
    }

    /**
     * 检查是否有空闲缓冲区可用
     * 
     * @return 如果有空闲缓冲区则返回true
     */
    bool Reader::bufferIsFree() {
        bool isFree;
        {
            contextSet(CONTEXT::MUTEX, REASON::READER_CHECK_FREE);
            std::unique_lock<std::mutex> const lck(mtx);
            isFree = (ctx->bufferSizeFree > 0);
        }
        contextSet(CONTEXT::CPU);
        return isFree;
    }

    /**
     * 将重做日志头部信息格式化为易读的文本
     * 
     * @param ss 输出流
     * @param path 日志文件路径
     */
    void Reader::printHeaderInfo(std::ostringstream& ss, const std::string& path) const {
        // 读取SID (数据库系统标识符)
        char SID[9];
        memcpy(reinterpret_cast<void*>(SID),
               reinterpret_cast<const void*>(headerBuffer + blockSize + 28), 8);
        SID[8] = 0;

        // 输出基本日志信息
        ss << "DUMP OF REDO FROM FILE '" << path << "'\n";
        if (ctx->version >= RedoLogRecord::REDO_VERSION_12_2)
            ss << " Container ID: 0\n Container UID: 0\n";
        ss << " Opcodes *.*\n";
        if (ctx->version >= RedoLogRecord::REDO_VERSION_12_2)
            ss << " Container ID: 0\n Container UID: 0\n";
        ss << " RBAs: 0x000000.00000000.0000 thru 0xffffffff.ffffffff.ffff\n";
        if (ctx->version < RedoLogRecord::REDO_VERSION_12_2)
            ss << " SCNs: scn: 0x0000.00000000 thru scn: 0xffff.ffffffff\n";
        else
            ss << " SCNs: scn: 0x0000000000000000 thru scn: 0xffffffffffffffff\n";
        ss << " Times: creation thru eternity\n";

        // 读取头部详细信息
        const uint32_t dbid = ctx->read32(headerBuffer + blockSize + 24);
        const uint32_t controlSeq = ctx->read32(headerBuffer + blockSize + 36);
        const uint32_t fileSizeHeader = ctx->read32(headerBuffer + blockSize + 40);
        const uint16_t fileNumber = ctx->read16(headerBuffer + blockSize + 48);

        // 输出详细头部信息
        ss << " FILE HEADER:\n" <<
           "\tCompatibility Vsn = " << std::dec << compatVsn << "=0x" << std::hex << compatVsn << '\n' <<
           "\tDb ID=" << std::dec << dbid << "=0x" << std::hex << dbid << ", Db Name='" << SID << "'\n" <<
           "\tActivation ID=" << std::dec << activation << "=0x" << std::hex << activation << '\n' <<
           "\tControl Seq=" << std::dec << controlSeq << "=0x" << std::hex << controlSeq << ", File size=" << std::dec << fileSizeHeader << "=0x" <<
           std::hex << fileSizeHeader << '\n' <<
           "\tFile Number=" << std::dec << fileNumber << ", Blksiz=" << std::dec << blockSize << ", File Type=2 LOG\n";

        // 输出序列号和描述信息
        const Seq seq = Seq(ctx->read32(headerBuffer + blockSize + 8));
        uint8_t descrip[65];
        memcpy(reinterpret_cast<void*>(descrip),
               reinterpret_cast<const void*>(headerBuffer + blockSize + 92), 64);
        descrip[64] = 0;
        const uint16_t thread = ctx->read16(headerBuffer + blockSize + 176);
        const uint32_t hws = ctx->read32(headerBuffer + blockSize + 172);
        const uint8_t eot = headerBuffer[blockSize + 204];
        const uint8_t dis = headerBuffer[blockSize + 205];

        ss << R"( descrip:")" << descrip << R"(")" << '\n' <<
           " thread: " << std::dec << thread <<
           " nab: 0x" << std::hex << numBlocksHeader <<
           " seq: " << seq.toStringHex(8) <<
           " hws: 0x" << std::hex << hws <<
           " eot: " << std::dec << static_cast<uint>(eot) <<
           " dis: " << std::dec << static_cast<uint>(dis) << '\n';

        // 读取SCN相关信息
        const Scn resetlogsScn = ctx->readScn(headerBuffer + blockSize + 164);
        const typeResetlogs prevResetlogsCnt = ctx->read32(headerBuffer + blockSize + 292);
        const Scn prevResetlogsScn = ctx->readScn(headerBuffer + blockSize + 284);
        const Scn enabledScn = ctx->readScn(headerBuffer + blockSize + 208);
        const Time enabledTime(ctx->read32(headerBuffer + blockSize + 216));
        const Scn threadClosedScn = ctx->readScn(headerBuffer + blockSize + 220);
        const Time threadClosedTime(ctx->read32(headerBuffer + blockSize + 228));
        const Scn termialRecScn = ctx->readScn(headerBuffer + blockSize + 240);
        const Time termialRecTime(ctx->read32(headerBuffer + blockSize + 248));
        const Scn mostRecentScn = ctx->readScn(headerBuffer + blockSize + 260);
        const typeSum chSum = ctx->read16(headerBuffer + blockSize + 14);
        const typeSum chSum2 = calcChSum(headerBuffer + blockSize, blockSize);

        // 根据版本输出SCN信息格式
        if (ctx->version < RedoLogRecord::REDO_VERSION_12_2) {
            ss << " resetlogs count: 0x" << std::hex << resetlogs << " scn: " << resetlogsScn.to48() <<
               " (" << resetlogsScn.toString() << ")\n" <<
               " prev resetlogs count: 0x" << std::hex << prevResetlogsCnt << " scn: " << prevResetlogsScn.to48() <<
               " (" << prevResetlogsScn.toString() << ")\n" <<
               " Low  scn: " << firstScnHeader.to48() << " (" << firstScnHeader.toString() << ")" << " " << firstTimeHeader << '\n' <<
               " Next scn: " << nextScnHeader.to48() << " (" << nextScn.toString() << ")" << " " << nextTime << '\n' <<
               " Enabled scn: " << enabledScn.to48() << " (" << enabledScn.toString() << ")" << " " << enabledTime << '\n' <<
               " Thread closed scn: " << threadClosedScn.to48() << " (" << threadClosedScn.toString() << ")" <<
               " " << threadClosedTime << '\n' <<
               " Disk cksum: 0x" << std::hex << chSum << " Calc cksum: 0x" << std::hex << chSum2 << '\n' <<
               " Terminal recovery stop scn: " << termialRecScn.to48() << '\n' <<
               " Terminal recovery  " << termialRecTime << '\n' <<
               " Most recent redo scn: " << mostRecentScn.to48() << '\n';
        } else {
            const Scn realNextScn = ctx->readScn(headerBuffer + blockSize + 272);

            ss << " resetlogs count: 0x" << std::hex << resetlogs << " scn: " << resetlogsScn.to64() << '\n' <<
               " prev resetlogs count: 0x" << std::hex << prevResetlogsCnt << " scn: " << prevResetlogsScn.to64() << '\n' <<
               " Low  scn: " << firstScnHeader.to64() << " " << firstTimeHeader << '\n' <<
               " Next scn: " << nextScnHeader.to64() << " " << nextTime << '\n' <<
               " Enabled scn: " << enabledScn.to64() << " " << enabledTime << '\n' <<
               " Thread closed scn: " << threadClosedScn.to64() << " " << threadClosedTime << '\n' <<
               " Real next scn: " << realNextScn.to64() << '\n' <<
               " Disk cksum: 0x" << std::hex << chSum << " Calc cksum: 0x" << std::hex << chSum2 << '\n' <<
               " Terminal recovery stop scn: " << termialRecScn.to64() << '\n' <<
               " Terminal recovery  " << termialRecTime << '\n' <<
               " Most recent redo scn: " << mostRecentScn.to64() << '\n';
        }

        // 输出其他标志和信息
        const uint32_t largestLwn = ctx->read32(headerBuffer + blockSize + 268);
        ss << " Largest LWN: " << std::dec << largestLwn << " blocks\n";

        const uint32_t miscFlags = ctx->read32(headerBuffer + blockSize + 236);
        const char* endOfRedo;
        if ((miscFlags & FLAGS_END) != 0)
            endOfRedo = "Yes";
        else
            endOfRedo = "No";
        if ((miscFlags & FLAGS_CLOSEDTHREAD) != 0)
            ss << " FailOver End-of-redo stream : " << endOfRedo << '\n';
        else
            ss << " End-of-redo stream : " << endOfRedo << '\n';

        // 输出各种特殊标志
        if ((miscFlags & FLAGS_ASYNC) != 0)
            ss << " Archivelog created using asynchronous network transmittal" << '\n';

        if ((miscFlags & FLAGS_NODATALOSS) != 0)
            ss << " No ctx-loss mode\n";

        if ((miscFlags & FLAGS_RESYNC) != 0)
            ss << " Resynchronization mode\n";
        else
            ss << " Unprotected mode\n";

        if ((miscFlags & FLAGS_CLOSEDTHREAD) != 0)
            ss << " Closed thread archival\n";

        if ((miscFlags & FLAGS_MAXPERFORMANCE) != 0)
            ss << " Maximize performance mode\n";

        ss << " Miscellaneous flags: 0x" << std::hex << miscFlags << '\n';

        // 12.2版本及以上的附加标志
        if (ctx->version >= RedoLogRecord::REDO_VERSION_12_2) {
            const uint32_t miscFlags2 = ctx->read32(headerBuffer + blockSize + 296);
            ss << " Miscellaneous second flags: 0x" << std::hex << miscFlags2 << '\n';
        }

        // 输出线程相关信息
        auto thr = static_cast<int32_t>(ctx->read32(headerBuffer + blockSize + 432));
        const auto seq2 = static_cast<int32_t>(ctx->read32(headerBuffer + blockSize + 436));
        const Scn scn2 = ctx->readScn(headerBuffer + blockSize + 440);
        const uint8_t zeroBlocks = headerBuffer[blockSize + 206];
        const uint8_t formatId = headerBuffer[blockSize + 207];
        if (ctx->version < RedoLogRecord::REDO_VERSION_12_2)
            ss << " Thread internal enable indicator: thr: " << std::dec << thr << "," <<
               " seq: " << std::dec << seq2 <<
               " scn: " << scn2.to48() << '\n' <<
               " Zero blocks: " << std::dec << static_cast<uint>(zeroBlocks) << '\n' <<
               " Format ID is " << std::dec << static_cast<uint>(formatId) << '\n';
        else
            ss << " Thread internal enable indicator: thr: " << std::dec << thr << "," <<
               " seq: " << std::dec << seq2 <<
               " scn: " << scn2.to64() << '\n' <<
               " Zero blocks: " << std::dec << static_cast<uint>(zeroBlocks) << '\n' <<
               " Format ID is " << std::dec << static_cast<uint>(formatId) << '\n';

        // 输出备用应用延迟信息
        const uint32_t standbyApplyDelay = ctx->read32(headerBuffer + blockSize + 280);
        if (standbyApplyDelay > 0)
            ss << " Standby Apply Delay: " << std::dec << standbyApplyDelay << " minute(s) \n";

        // 输出备用日志关闭时间
        const Time standbyLogCloseTime(ctx->read32(headerBuffer + blockSize + 304));
        if (standbyLogCloseTime.getVal() > 0)
            ss << " Standby Log Close Time:  " << standbyLogCloseTime << '\n';

        // 输出重做日志密钥
        ss << " redo log key is ";
        for (uint i = 448; i < 448 + 16; ++i)
            ss << std::setfill('0') << std::setw(2) << std::hex << static_cast<uint>(headerBuffer[blockSize + i]);
        ss << '\n';

        // 输出重做日志密钥标志和启用的重做线程数
        const uint16_t redoKeyFlag = ctx->read16(headerBuffer + blockSize + 480);
        ss << " redo log key flag is " << std::dec << redoKeyFlag << '\n';
        const uint16_t enabledRedoThreads = 1; // TODO: 找到字段位置/大小
        ss << " Enabled redo threads: " << std::dec << enabledRedoThreads << " \n";
    }

    /**
     * 获取当前块大小
     * 
     * @return 块大小(字节)
     */
    uint Reader::getBlockSize() const {
        return blockSize;
    }

    /**
     * 获取当前缓冲区起始位置
     * 
     * @return 缓冲区起始文件偏移量
     */
    FileOffset Reader::getBufferStart() const {
        return FileOffset(bufferStart);
    }

    /**
     * 获取当前缓冲区结束位置
     * 
     * @return 缓冲区结束文件偏移量
     */
    FileOffset Reader::getBufferEnd() const {
        return FileOffset(bufferEnd);
    }

    /**
     * 获取当前读取操作的返回码
     * 
     * @return 返回码
     */
    Reader::REDO_CODE Reader::getRet() const {
        return ret;
    }

    /**
     * 获取第一个SCN
     * 
     * @return 第一个SCN
     */
    Scn Reader::getFirstScn() const {
        return firstScn;
    }

    /**
     * 获取头部的第一个SCN
     * 
     * @return 头部的第一个SCN
     */
    Scn Reader::getFirstScnHeader() const {
        return firstScnHeader;
    }

    /**
     * 获取下一个SCN
     * 
     * @return 下一个SCN
     */
    Scn Reader::getNextScn() const {
        return nextScn;
    }

    /**
     * 获取下一个时间
     * 
     * @return 下一个时间
     */
    Time Reader::getNextTime() const {
        return nextTime;
    }

    /**
     * 获取块数量
     * 
     * @return 块数量
     */
    typeBlk Reader::getNumBlocks() const {
        return numBlocksHeader;
    }

    /**
     * 获取组号
     * 
     * @return 组号
     */
    int Reader::getGroup() const {
        return group;
    }

    /**
     * 获取序列号
     * 
     * @return 序列号
     */
    Seq Reader::getSequence() const {
        return sequence;
    }

    /**
     * 获取重置日志计数
     * 
     * @return 重置日志计数
     */
    typeResetlogs Reader::getResetlogs() const {
        return resetlogs;
    }

    /**
     * 获取激活ID
     * 
     * @return 激活ID
     */
    typeActivation Reader::getActivation() const {
        return activation;
    }

    /**
     * 获取读取的总字节数
     * 
     * @return 总字节数
     */
    uint64_t Reader::getSumRead() const {
        return sumRead;
    }

    /**
     * 获取读取的总时间
     * 
     * @return 总时间
     */
    uint64_t Reader::getSumTime() const {
        return sumTime;
    }

    /**
     * 设置返回码
     * 
     * @param newRet 新的返回码
     */
    void Reader::setRet(REDO_CODE newRet) {
        ret = newRet;
    }

    /**
     * 设置缓冲区的起始和结束位置
     * 
     * @param newBufferStart 新的缓冲区起始位置
     * @param newBufferEnd 新的缓冲区结束位置
     */
    void Reader::setBufferStartEnd(FileOffset newBufferStart, FileOffset newBufferEnd) {
        bufferStart = newBufferStart.getData();
        bufferEnd = newBufferEnd.getData();
    }

    /**
     * 检查重做日志是否有效
     * 
     * @return 如果重做日志有效则返回true
     */
    bool Reader::checkRedoLog() {
        contextSet(CONTEXT::MUTEX, REASON::READER_CHECK_REDO);
        std::unique_lock<std::mutex> lck(mtx);
        status = STATUS::CHECK;
        sequence = Seq::zero();
        firstScn = Scn::none();
        nextScn = Scn::none();
        condBufferFull.notify_all();
        condReaderSleeping.notify_all();

        while (status == STATUS::CHECK) {
            if (ctx->softShutdown)
                break;
            if (unlikely(ctx->isTraceSet(Ctx::TRACE::SLEEP)))
                ctx->logTrace(Ctx::TRACE::SLEEP, "Reader:checkRedoLog");
            contextSet(CONTEXT::WAIT, REASON::READER_CHECK);
            condParserSleeping.wait(lck);
        }
        contextSet(CONTEXT::CPU);
        return (ret == REDO_CODE::OK);
    }

    /**
     * 更新重做日志
     * 
     * @return 如果更新成功则返回true
     */
    bool Reader::updateRedoLog() {
        for (;;) {
            contextSet(CONTEXT::MUTEX, REASON::READER_UPDATE_REDO1);
            std::unique_lock<std::mutex> lck(mtx);
            status = STATUS::UPDATE;
            condBufferFull.notify_all();
            condReaderSleeping.notify_all();

            while (status == STATUS::UPDATE) {
                if (ctx->softShutdown)
                    break;
                if (unlikely(ctx->isTraceSet(Ctx::TRACE::SLEEP)))
                    ctx->logTrace(Ctx::TRACE::SLEEP, "Reader:updateRedoLog");
                contextSet(CONTEXT::WAIT);
                condParserSleeping.wait(lck);
                contextSet(CONTEXT::MUTEX, REASON::READER_UPDATE_REDO2);
            }

            if (ret == REDO_CODE::EMPTY) {
                contextSet(CONTEXT::WAIT, REASON::READER_EMPTY);
                condParserSleeping.wait_for(lck, std::chrono::microseconds(ctx->redoReadSleepUs));
                contextSet(CONTEXT::MUTEX, REASON::READER_UPDATE_REDO3);
                continue;
            }

            contextSet(CONTEXT::CPU);
            return (ret == REDO_CODE::OK);
        }
    }

    /**
     * 设置状态为读取
     */
    void Reader::setStatusRead() {
        {
            contextSet(CONTEXT::MUTEX, REASON::READER_SET_READ);
            std::unique_lock<std::mutex> const lck(mtx);
            status = STATUS::READ;
            condBufferFull.notify_all();
            condReaderSleeping.notify_all();
        }
        contextSet(CONTEXT::CPU);
    }

    /**
     * 确认读取的数据
     * 
     * @param confirmedBufferStart 确认的缓冲区起始位置
     */
    void Reader::confirmReadData(FileOffset confirmedBufferStart) {
        contextSet(CONTEXT::MUTEX, REASON::READER_CONFIRM);
        {
            std::unique_lock<std::mutex> const lck(mtx);
            bufferStart = confirmedBufferStart.getData();
            if (status == STATUS::READ) {
                condBufferFull.notify_all();
            }
        }
        contextSet(CONTEXT::CPU);
    }

    /**
     * 检查是否已完成读取
     * 
     * @param t 当前线程
     * @param confirmedBufferStart 确认的缓冲区起始位置
     * @return 如果已完成读取则返回true
     */
    bool Reader::checkFinished(Thread* t, FileOffset confirmedBufferStart) {
        t->contextSet(CONTEXT::MUTEX, REASON::READER_CHECK_FINISHED);
        {
            std::unique_lock<std::mutex> lck(mtx);
            if (bufferStart < confirmedBufferStart.getData())
                bufferStart = confirmedBufferStart.getData();

            // All work done
            if (confirmedBufferStart.getData() == bufferEnd) {
                if (ret == REDO_CODE::STOPPED || ret == REDO_CODE::OVERWRITTEN || ret == REDO_CODE::FINISHED || status == STATUS::SLEEPING) {
                    t->contextSet(CONTEXT::CPU);
                    return true;
                }
                if (unlikely(ctx->isTraceSet(Ctx::TRACE::SLEEP)))
                    ctx->logTrace(Ctx::TRACE::SLEEP, "Reader:checkFinished");
                t->contextSet(CONTEXT::WAIT, REASON::READER_FINISHED);
                condParserSleeping.wait(lck);
            }
        }
        t->contextSet(CONTEXT::CPU);
        return false;
    }
}
