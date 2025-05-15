/**
 * 检查点管理类头文件
 * 
 * 该文件定义了Checkpoint类，用于管理检查点文件的创建、跟踪和更新。
 * 检查点文件用于存储重做日志解析的当前状态，以便系统重启时能从断点恢复。
 *
 * @file Checkpoint.h
 * @author Adam Leszczynski (aleszczynski@bersler.com)
 * @copyright Copyright (C) 2018-2025 Adam Leszczynski
 * @license GPL-3.0
 */

#ifndef CHECKPOINT_H_
#define CHECKPOINT_H_

#include <condition_variable>
#include <mutex>
#include <set>
#include <vector>
#include <unordered_map>

#include "../common/Thread.h"
#include "../common/types/Types.h"
#include "../common/types/Xid.h"

namespace OpenLogReplicator {
    class Metadata;
    class DbIncarnation;
    class TransactionBuffer;

    /**
     * 检查点管理类 - 负责管理检查点文件的创建和更新
     */
    class Checkpoint final : public Thread {
    public:
        /** 配置文件最大尺寸限制 */
        static constexpr off_t CONFIG_FILE_MAX_SIZE = 1048576;

    protected:
        Metadata* metadata;             // 元数据管理器
        std::mutex mtx;                 // 互斥锁
        std::condition_variable condLoop; // 条件变量
        char* configFileBuffer{nullptr}; // 配置文件缓冲区
        std::string configFileName;      // 配置文件名
        time_t configFileChange;         // 配置文件最后修改时间

        /**
         * 跟踪配置文件变化
         */
        void trackConfigFile();
        
        /**
         * 更新配置文件内容
         */
        void updateConfigFile();

    public:
        /**
         * 构造函数
         * 
         * @param newCtx 上下文对象
         * @param newMetadata 元数据对象
         * @param newAlias 别名
         * @param newConfigFileName 配置文件名
         * @param newConfigFileChange 配置文件修改时间
         */
        Checkpoint(Ctx* newCtx, Metadata* newMetadata, std::string newAlias, std::string newConfigFileName, time_t newConfigFileChange);
        
        /**
         * 析构函数
         */
        ~Checkpoint() override;

        /**
         * 唤醒线程
         */
        void wakeUp() override;
        
        /**
         * 线程主函数
         */
        void run() override;

        /**
         * 获取线程名称
         * 
         * @return 线程名称
         */
        std::string getName() const override {
            return {"Checkpoint"};
        }
    };
}

#endif
