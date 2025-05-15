#ifndef TRANSACTION_CHUNK_H_
#define TRANSACTION_CHUNK_H_

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "../common/types/FileOffset.h"
#include "../common/types/Scn.h"
#include "../common/types/Seq.h"
#include "../common/types/Time.h"
#include "../common/types/TransactionChunkId.h"
#include "../common/types/Types.h"
#include "../common/types/Xid.h"
#include "../parser/RedoLogRecord.h"

namespace OpenLogReplicator {
    // 前向声明
    class Ctx;
    class Transaction;
    class TransactionBuffer;

    // 事务块类 - 表示事务的一部分，用于处理大型事务
    class TransactionChunk final {
    public:
        // 事务块操作类型
        enum class OPERATION : uint8_t {
            UNKNOWN = 0,        // 未知操作
            COMMIT = 1,         // 提交
            DDL = 2,            // DDL操作
            DML = 3,            // DML操作
            ROLLBACK = 4,       // 回滚
            BEGIN = 5           // 开始
        };

        // 事务块状态
        enum class STATUS : uint8_t {
            NOT_READY = 0,      // 未就绪
            READY_TO_READ = 1,  // 准备读取
            READY_TO_PROCESS = 2, // 准备处理
            READY_TO_FREE = 3   // 准备释放
        };

        // 基本属性
        Ctx* ctx;               // 上下文
        TransactionBuffer* transactionBuffer; // 事务缓冲区
        TransactionChunkId chunkId; // 块ID
        Transaction* transaction;   // 所属事务
        uint64_t prevSize;      // 前一个大小
        typeScn scn;            // SCN
        typeScn commitScn;      // 提交SCN
        Time time;              // 时间
        OPERATION operation;    // 操作类型
        std::atomic<STATUS> status; // 状态(原子操作)
        FileOffset offset;      // 文件偏移
        uint64_t redoSize;      // 重做大小
        Seq sequence;           // 序列号
        uint64_t flags;         // 标志位
        
        // 事务块间链接
        TransactionChunk* next; // 下一个块

        // 重做记录存储
        RedoLogRecord** redoLogs1; // 重做日志记录数组1
        RedoLogRecord** redoLogs2; // 重做日志记录数组2
        uint64_t redoLogs1Size;   // 重做日志记录数组1大小
        uint64_t redoLogs2Size;   // 重做日志记录数组2大小
        uint64_t redoLogs1Max;    // 重做日志记录数组1最大大小
        uint64_t redoLogs2Max;    // 重做日志记录数组2最大大小
        
        // 元数据存储
        void* metaData;         // 元数据指针

        // 构造与析构
        TransactionChunk(Ctx* newCtx, TransactionBuffer* newTransactionBuffer, Transaction* newTransaction, TransactionChunkId newChunkId);
        ~TransactionChunk();

        // 事务操作
        void flush(bool commit, Scn commitScn);
        void setRollback();
        void close();

        // 状态管理
        void setStatusNotReady();
        void setStatusReadyToRead();
        void setStatusReadyToProcess();
        void setStatusReadyToFree();
        
        // 状态查询
        [[nodiscard]] bool isStatusNotReady() const;
        [[nodiscard]] bool isStatusReadyToRead() const;
        [[nodiscard]] bool isStatusReadyToProcess() const;
        [[nodiscard]] bool isStatusReadyToFree() const;

        // 添加重做日志记录
        void addRedoLog(RedoLogRecord* redoLogRecord, OPERATION operation);
        void addRedoLog2(RedoLogRecord* redoLogRecord);
    };
}

#endif
