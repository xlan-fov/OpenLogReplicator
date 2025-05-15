/* Header for Thread class
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

#ifndef THREAD_H_
#define THREAD_H_

#include <atomic>
#include <sys/time.h>

#include "Clock.h"
#include "Ctx.h"
#include "types/Types.h"

namespace OpenLogReplicator {
    class Ctx;

    // Thread类 - 所有工作线程的基类
    class Thread {
    protected:
        virtual void run() = 0;  // 纯虚函数，由子类实现

    public:
        // 上下文枚举 - 表示线程在什么环境中执行
        enum class CONTEXT : unsigned char {
            NONE,  // 无上下文
            CPU,   // CPU计算上下文
            OS,    // 操作系统上下文
            MUTEX, // 互斥锁上下文
            WAIT,  // 等待上下文
            SLEEP, // 睡眠上下文
            MEM,   // 内存上下文
            TRAN,  // 事务上下文
            CHKPT, // 检查点上下文
            NUM    // 上下文数量
        };
        
        // 原因枚举 - 表示线程进入某种状态的原因
        enum class REASON : unsigned char {
            NONE,
            // MUTEX原因 - 与互斥锁相关的操作
            BUILDER_RELEASE, BUILDER_ROTATE, BUILDER_COMMIT, CHECKPOINT_RUN, // 1-4
            // ...其他互斥锁相关原因...
            
            // SLEEP原因 - 与线程睡眠相关的操作
            CHECKPOINT_NO_WORK, MEMORY_EXHAUSTED, METADATA_WAIT_WRITER, METADATA_WAIT_FOR_REPLICATOR, READER_CHECK, // 55-59
            // ...其他睡眠相关原因...
            
            // 其他原因组
            OS, MEM, TRAN, CHKPT, // 67-70
            
            // 结束标记
            NUM = 255  // 原因数量
        };

        Ctx* ctx;                    // 上下文对象
        pthread_t pthread{0};        // POSIX线程标识
        std::string alias;           // 线程别名
        std::atomic<bool> finished{false};  // 线程是否已完成

        // 构造函数和析构函数
        explicit Thread(Ctx* newCtx, std::string newAlias);
        virtual ~Thread() = default;

        // 唤醒线程
        virtual void wakeUp();
        
        // 静态运行函数，作为pthread_create的入口点
        static void* runStatic(void* thread);

        // 线程性能跟踪相关
#ifdef THREAD_INFO
        static constexpr bool contextCompiled = true;  // 是否编译了线程信息
#else
        static constexpr bool contextCompiled = false;
#endif
        time_ut contextTimeLast{0};  // 上次上下文切换时间
        time_ut contextTime[static_cast<uint>(CONTEXT::NUM)]{0};  // 各上下文花费时间
        time_ut contextCnt[static_cast<uint>(CONTEXT::NUM)]{0};   // 各上下文进入次数
        uint64_t reasonCnt[static_cast<uint>(REASON::NUM)]{0};    // 各原因计数
        REASON curReason{REASON::NONE};     // 当前原因
        CONTEXT curContext{CONTEXT::NONE};  // 当前上下文
        uint64_t contextSwitches{0};        // 上下文切换次数

        // 获取线程名称
        virtual std::string getName() const = 0;

        // 上下文管理方法
        void contextRun() {
            contextStart();
            run();
            contextStop();
        }

        // 开始上下文跟踪
        void contextStart() {
            if constexpr  (contextCompiled) {
                contextTimeLast = ctx->clock->getTimeUt();
            }
        }

        // 设置当前上下文
        void contextSet(CONTEXT context, REASON reason = REASON::NONE) {
            if constexpr (contextCompiled) {
                ++contextSwitches;
                const time_ut contextTimeNow = ctx->clock->getTimeUt();
                contextTime[static_cast<uint>(curContext)] += contextTimeNow - contextTimeLast;
                ++contextCnt[static_cast<uint>(curContext)];
                ++reasonCnt[static_cast<uint>(reason)];
                curReason = reason;
                curContext = context;
                contextTimeLast = contextTimeNow;
            }
        }

        // 停止上下文跟踪并输出统计信息
        void contextStop() {
            if constexpr (contextCompiled) {
                ++contextSwitches;
                const time_ut contextTimeNow = ctx->clock->getTimeUt();
                contextTime[static_cast<uint>(curContext)] += contextTimeNow - contextTimeLast;
                ++contextCnt[static_cast<uint>(curContext)];

                std::string msg =
                        "thread: " + alias +
                        " cpu: " + std::to_string(contextTime[static_cast<uint>(CONTEXT::CPU)]) +
                        "/" + std::to_string(contextCnt[static_cast<uint>(CONTEXT::CPU)]) +
                        " os: " + std::to_string(contextTime[static_cast<uint>(CONTEXT::OS)]) +
                        "/" + std::to_string(contextCnt[static_cast<uint>(CONTEXT::OS)]) +
                        " mtx: " + std::to_string(contextTime[static_cast<uint>(CONTEXT::MUTEX)]) +
                        "/" + std::to_string(contextCnt[static_cast<uint>(CONTEXT::MUTEX)]) +
                        " wait: " + std::to_string(contextTime[static_cast<uint>(CONTEXT::WAIT)]) +
                        "/" + std::to_string(contextCnt[static_cast<uint>(CONTEXT::WAIT)]) +
                        " sleep: " + std::to_string(contextTime[static_cast<uint>(CONTEXT::SLEEP)]) +
                        "/" + std::to_string(contextCnt[static_cast<uint>(CONTEXT::SLEEP)]) +
                        " mem: " + std::to_string(contextTime[static_cast<uint>(CONTEXT::MEM)]) +
                        "/" + std::to_string(contextCnt[static_cast<uint>(CONTEXT::MEM)]) +
                        " tran: " + std::to_string(contextTime[static_cast<uint>(CONTEXT::TRAN)]) +
                        "/" + std::to_string(contextCnt[static_cast<uint>(CONTEXT::TRAN)]) +
                        " chkpt: " + std::to_string(contextTime[static_cast<uint>(CONTEXT::CHKPT)]) +
                        "/" + std::to_string(contextCnt[static_cast<uint>(CONTEXT::CHKPT)]) +
                        " switches: " + std::to_string(contextSwitches) + " reasons:";
                for (uint reason = static_cast<uint>(REASON::NONE); reason < static_cast<int>(REASON::NUM); ++reason) {
                    if (reasonCnt[reason] == 0)
                        continue;

                    msg += " " + std::to_string(reason) + "/" + std::to_string(reasonCnt[reason]);
                }
                ctx->info(0, msg);
            }
        }
    };
}

#endif
