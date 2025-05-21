/* Header for Reader class
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

#include <atomic>
#include <vector>

#include "../common/Thread.h"
#include "../common/types/FileOffset.h"
#include "../common/types/Scn.h"
#include "../common/types/Seq.h"
#include "../common/types/Time.h"
#include "../common/types/Types.h"

#ifndef READER_H_
#define READER_H_

namespace OpenLogReplicator {
    class Reader : public Thread {
    public:
        enum class REDO_CODE : unsigned char {
            OK, OVERWRITTEN, FINISHED, STOPPED, SHUTDOWN, EMPTY, ERROR_READ, ERROR_WRITE, ERROR_SEQUENCE, ERROR_CRC, ERROR_BLOCK, ERROR_BAD_DATA,
            ERROR, CNT
        };

    protected:
        // 标志常量定义
        static constexpr uint64_t FLAGS_END{0x0008};         // 结束标志
        static constexpr uint64_t FLAGS_ASYNC{0x0100};       // 异步标志
        static constexpr uint64_t FLAGS_NODATALOSS{0x0200};  // 无数据丢失标志
        static constexpr uint64_t FLAGS_RESYNC{0x0800};      // 重新同步标志
        static constexpr uint64_t FLAGS_CLOSEDTHREAD{0x1000}; // 线程关闭标志
        static constexpr uint64_t FLAGS_MAXPERFORMANCE{0x2000}; // 最大性能标志

        // 常量定义
        static constexpr uint PAGE_SIZE_MAX{4096};           // 最大页面大小
        static constexpr uint BAD_CDC_MAX_CNT{20};           // 最大错误CDC计数

        // 基础属性
        std::string database;                  // 数据库名称
        int fileCopyDes{-1};                   // 文件复制描述符
        uint64_t fileSize{0};                  // 文件大小
        Seq fileCopySequence;                  // 文件复制序列
        bool hintDisplayed{false};             // 是否显示提示
        bool configuredBlockSum;               // 是否配置块校验和
        bool readBlocks{false};                // 是否读取块
        bool reachedZero{false};               // 是否达到零点
        std::string fileNameWrite;             // 写入文件名
        int group;                             // 组号
        Seq sequence;                          // 序列号
        typeBlk numBlocksHeader{Ctx::ZERO_BLK}; // 头部块数
        typeResetlogs resetlogs{0};            // 重置日志ID
        typeActivation activation{0};          // 激活ID
        uint8_t* headerBuffer{nullptr};        // 头部缓冲区
        uint32_t compatVsn{0};                 // 兼容版本
        Time firstTimeHeader{0};               // 首次时间头
        Scn firstScn{Scn::none()};             // 首个SCN
        Scn firstScnHeader{Scn::none()};       // 头部首个SCN
        Scn nextScn{Scn::none()};              // 下一个SCN - 日志文件中下一个可处理的系统变更号
        Scn nextScnHeader{Scn::none()};        // 头部下一个SCN - 从日志文件头部读取的下一个SCN值
        Time nextTime{0};                      // 下一个时间 - 下一个记录的时间戳
        uint blockSize{0};                     // 块大小 - 重做日志文件的块大小，通常为512字节的倍数
        uint64_t sumRead{0};                   // 读取总数 - 已读取的数据总字节数
        uint64_t sumTime{0};                   // 时间总数 - 读取操作消耗的总时间
        uint64_t bufferScan{0};                // 缓冲区扫描 - 缓冲区被扫描的次数
        uint lastRead{0};                      // 最后读取 - 最后一次读取的字节数
        time_ut lastReadTime{0};               // 最后读取时间 - 最后一次读取操作的时间戳
        time_ut readTime{0};                   // 读取时间 - 当前读取操作的时间戳
        time_ut loopTime{0};                   // 循环时间 - 主循环迭代的时间戳

        // 同步和线程控制
        std::mutex mtx;                        // 互斥锁 - 用于保护读取器共享资源的访问
        std::atomic<uint64_t> bufferStart{0};  // 缓冲区开始位置 - 当前读取缓冲区的起始偏移量
        std::atomic<uint64_t> bufferEnd{0};    // 缓冲区结束位置 - 当前读取缓冲区的结束偏移量
        std::atomic<STATUS> status{STATUS::SLEEPING}; // 状态 - 读取器当前的工作状态(睡眠、运行等)
        std::atomic<REDO_CODE> ret{REDO_CODE::OK}; // 返回码 - 上一次操作的返回状态码
        std::condition_variable condBufferFull;    // 缓冲区满条件变量 - 用于通知缓冲区已满
        std::condition_variable condReaderSleeping; // 读取器睡眠条件变量 - 用于唤醒睡眠中的读取器
        std::condition_variable condParserSleeping; // 解析器睡眠条件变量 - 用于唤醒睡眠中的解析器

        // 虚拟方法，需由子类实现
        virtual void redoClose() = 0;                    // 关闭重做日志 - 关闭当前打开的重做日志文件
        virtual REDO_CODE redoOpen() = 0;                // 打开重做日志 - 打开一个新的重做日志文件
        virtual int redoRead(uint8_t* buf, uint64_t offset, uint size) = 0; // 读取重做日志 - 从指定偏移量读取数据
        virtual uint readSize(uint prevRead);            // 获取读取大小 - 计算下一次读取的数据大小
        virtual REDO_CODE reloadHeaderRead();            // 重新读取头部 - 重新加载重做日志文件的头部信息

        // 实用方法
        REDO_CODE checkBlockHeader(uint8_t* buffer, typeBlk blockNumber, bool showHint); // 检查块头 - 验证块头的有效性和一致性
        REDO_CODE reloadHeader();                        // 重新加载头部 - 重新加载并解析日志文件头部
        bool read1();                                    // 读取1 - 第一阶段读取操作
        bool read2();                                    // 读取2 - 第二阶段读取操作
        void mainLoop();                                 // 主循环 - 读取器的主工作循环

    public:
        const static char* REDO_MSG[static_cast<uint>(REDO_CODE::CNT)];
        uint8_t** redoBufferList{nullptr};               // 重做缓冲区列表 - 用于存储读取的数据块
        std::vector<std::string> paths;                  // 路径列表 - 重做日志文件的路径列表
        std::string fileName;                            // 文件名 - 当前正在处理的文件名

        Reader(Ctx* newCtx, std::string newAlias, std::string newDatabase, int newGroup, bool newConfiguredBlockSum);
        ~Reader() override;

        void initialize();
        void wakeUp() override;
        void run() override;
        void bufferAllocate(uint num);
        void bufferFree(Thread* t, uint num);
        bool bufferIsFree();
        typeSum calcChSum(uint8_t* buffer, uint size) const;
        void printHeaderInfo(std::ostringstream& ss, const std::string& path) const;
        [[nodiscard]] uint getBlockSize() const;
        [[nodiscard]] FileOffset getBufferStart() const;
        [[nodiscard]] FileOffset getBufferEnd() const;
        [[nodiscard]] REDO_CODE getRet() const;
        [[nodiscard]] Scn getFirstScn() const;
        [[nodiscard]] Scn getFirstScnHeader() const;
        [[nodiscard]] Scn getNextScn() const;
        [[nodiscard]] Time getNextTime() const;
        [[nodiscard]] typeBlk getNumBlocks() const;
        [[nodiscard]] int getGroup() const;
        [[nodiscard]] Seq getSequence() const;
        [[nodiscard]] typeResetlogs getResetlogs() const;
        [[nodiscard]] typeActivation getActivation() const;
        [[nodiscard]] uint64_t getSumRead() const;
        [[nodiscard]] uint64_t getSumTime() const;

        void setRet(REDO_CODE newRet);
        void setBufferStartEnd(FileOffset newBufferStart, FileOffset newBufferEnd);
        bool checkRedoLog();
        bool updateRedoLog();
        void setStatusRead();
        void confirmReadData(FileOffset confirmedBufferStart);
        [[nodiscard]] bool checkFinished(Thread* t, FileOffset confirmedBufferStart);
        virtual void showHint(Thread* t, std::string origPath, std::string mappedPath) const = 0;

        std::string getName() const override {
            return {"Reader: " + fileName};
        }
    };
}

#endif
