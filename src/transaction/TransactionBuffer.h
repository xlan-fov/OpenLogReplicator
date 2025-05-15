#ifndef TRANSACTION_BUFFER_H_
#define TRANSACTION_BUFFER_H_

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "../common/types/TransactionChunkId.h"
#include "../common/types/Xid.h"

namespace OpenLogReplicator {
    // 前向声明
    class Builder;
    class Ctx;
    class Parser;
    class RedoLogRecord;
    class Transaction;
    class TransactionChunk;

    // 事务缓冲区类 - 管理所有活动事务和内存分配
    class TransactionBuffer final {
    protected:
        Ctx* ctx;                      // 上下文
        Builder* builder;              // 构建器

        // 事务相关集合
        std::unordered_map<Xid, Transaction*, Xid::Hash> transactions; // 事务映射
        std::unordered_map<TransactionChunkId, TransactionChunk*, TransactionChunkId::Hash> transactionChunks; // 事务块映射
        std::mutex transactionsMtx;    // 事务互斥锁
        
        // 内存管理
        std::mutex memoryMtx;          // 内存互斥锁
        uint64_t commitChunksTotal;    // 提交块总数
        
        // 统计信息
        uint64_t ddlsTotal;            // DDL总数
        uint64_t dmlsTotal;            // DML总数
        uint64_t commitsTotal;         // 提交总数
        uint64_t rollbacksTotal;       // 回滚总数
        uint64_t recordDdlsTotal;      // 记录DDL总数
        uint64_t recordDmlsTotal;      // 记录DML总数
        
        // 线程相关
        std::atomic<bool> terminateCommitter; // 终止提交线程标志
        std::mutex committerMtx;       // 提交线程互斥锁
        std::condition_variable committerCv; // 提交线程条件变量
        uint64_t lastTransactionSize;  // 最后事务大小

    public:
        // 构造与析构
        TransactionBuffer(Ctx* newCtx, Builder* newBuilder);
        ~TransactionBuffer();

        // 初始化方法
        void initialize();
        
        // 事务处理
        Transaction* getTransaction(const Xid& xid, RedoLogRecord* redoLogRecord);
        bool freeChunks();
        void addCommitChunks(uint64_t chunks);
        
        // 提交线程
        void committer();
        void committerWaitForWork();
        void wakeUpCommitter();
        void stopCommitter();

        // 统计方法
        void addDdl();
        void addDml();
        void addCommit();
        void addRollback();
        void addRecordDdl();
        void addRecordDml();
        
        // 调试信息
        void showTranStatistics() const;
        void analyzeTran() const;
    };
}

#endif
