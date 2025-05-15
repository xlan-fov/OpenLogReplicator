#ifndef TRANSACTION_H_
#define TRANSACTION_H_

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "../common/types/Scn.h"
#include "../common/types/Time.h"
#include "../common/types/Types.h"
#include "../common/types/Xid.h"
#include "../parser/RedoLogRecord.h"

namespace OpenLogReplicator {
    // 前向声明
    class Ctx;
    class TransactionBuffer;
    class TransactionChunk;

    // 事务类 - 表示从重做日志中提取的完整事务信息
    class Transaction final {
    public:
        // 事务状态枚举
        enum class STATUS : uint64_t {
            NEW = 0,          // 新事务
            UPDATED = 1,      // 已更新
            PREPARED = 2,     // 已准备提交
            COMMITTED = 3,    // 已提交
            CHECKED = 4,      // 已检查
            PROCESSED = 5,    // 已处理
            BAD = 6,          // 错误事务
            ROLLBACKED = 7,   // 已回滚
            TWO_PHASE = 8,    // 两阶段提交
            TEMP_LOB_SINGLE = 9, // 临时LOB单一
            TEMP_LOB_MULTIPLE = 10, // 临时LOB多重
            WITH_ALT_LMN = 11, // 带有ALT LMN
            WITH_BIGDATA = 12  // 带有大数据
        };

        // 基本属性
        Xid xid;             // 事务ID
        std::atomic<STATUS> status; // 状态(原子操作)
        TransactionBuffer* transactionBuffer; // 事务缓冲区
        uint64_t flags;      // 标志位
        Time maxTime;        // 最大时间
        uint64_t maxRollbackSize; // 最大回滚大小
        std::atomic<uint64_t> lastChunkId; // 最后块ID(原子操作)
        uint64_t ddlStarts;  // DDL开始位置
        uint64_t ddlEnds;    // DDL结束位置
        std::string name;    // 事务名称
        Scn scn;             // 系统变更号
        Scn commitScn;       // 提交SCN
        TransactionChunk* firstChunk; // 第一个事务块
        TransactionChunk* lastChunk;  // 最后一个事务块
        std::mutex mtx;      // 互斥锁
        std::condition_variable condition; // 条件变量

        // 其他相关对象集合
        std::unordered_set<typeObj> objsUsed;      // 使用的对象集合
        std::unordered_set<Xid, Xid::Hash> lobDepend; // LOB依赖集合
        
        // 序列信息
        Seq startSequence;   // 开始序列
        Seq commitSequence;  // 提交序列

        static uint64_t count;  // 事务计数(静态)

        // 构造与析构
        Transaction(TransactionBuffer* newTransactionBuffer, const Xid& newXid);
        ~Transaction();

        // 事务操作方法
        void addLobDepend(Xid newXid);
        void addObj(typeObj newObj);
        void addDdl(RedoLogRecord* redoLogRecord);
        void addDml(RedoLogRecord* redoLogRecord);
        void close();
        
        // 状态设置和检查
        void setNew();
        void setUpdated();
        void setPrepared();
        void setCommitted();
        void setChecked();
        void setProcessed();
        void setRollbacked();
        void setBad();
        void setTwoPhase();
        void setWithAltLmn();
        void setWithBigdata();
        [[nodiscard]] bool isNew() const;
        [[nodiscard]] bool isUpdated() const;
        [[nodiscard]] bool isPrepared() const;
        [[nodiscard]] bool isCommitted() const;
        [[nodiscard]] bool isChecked() const;
        [[nodiscard]] bool isProcessed() const;
        [[nodiscard]] bool isBad() const;
        [[nodiscard]] bool isRollbacked() const;
        [[nodiscard]] bool isTwoPhase() const;
        [[nodiscard]] bool isTempLobSingle() const;
        [[nodiscard]] bool isTempLobMultiple() const;
        [[nodiscard]] bool isWithAltLmn() const;
        [[nodiscard]] bool isWithBigdata() const;
        
        // 临时LOB处理
        void setTempLobSingle();
        void setTempLobMultiple();

        // 标志操作
        [[nodiscard]] bool isFlagSet(uint64_t flag) const;
        void setFlag(uint64_t flag);
        void unsetFlag(uint64_t flag);

        // 依赖检查
        [[nodiscard]] bool isDependenciesCompleted() const;
        
        // 信息转换
        [[nodiscard]] std::string statusToString() const;

        // 添加事务块
        void addChunk(TransactionChunk* chunk, uint64_t chunkId);
    };
}

#endif
