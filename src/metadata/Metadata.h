/**
 * 元数据管理类头文件
 *
 * 该文件定义了Metadata类，用于管理Oracle数据库及其重做日志的元信息。
 * 包括数据库参数、检查点信息、架构信息等。
 *
 * @file Metadata.h
 * @author Adam Leszczynski
 * @copyright Copyright (C) 2018-2025 Adam Leszczynski
 * @license GPL-3.0
 */
#ifndef METADATA_H_
#define METADATA_H_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <set>
#include <unordered_map>
#include <vector>

#include "../common/Ctx.h"
#include "../common/DbTable.h"
#include "../common/types/FileOffset.h"
#include "../common/types/Seq.h"
#include "../common/types/Time.h"
#include "../common/types/Xid.h"

namespace OpenLogReplicator {
    // 前向声明
    class Ctx;
    class DbIncarnation;
    class Locales;
    class RedoLog;
    class Schema;
    class SchemaElement;
    class Serializer;
    class State;
    class StateDisk;
    class Thread;

    /**
     * 元数据类 - 管理Oracle数据库及其日志元信息
     */
    class Metadata final {
    protected:
        // 线程同步条件变量
        std::condition_variable condReplicator; // 复制器线程条件变量
        std::condition_variable condWriter;     // 写入器线程条件变量
        
        // 检查点文件最大大小
        static constexpr uint64_t CHECKPOINT_SCHEMA_FILE_MAX_SIZE = 2147483648;

    public:
        /**
         * 复制器状态枚举类型
         */
        enum class STATUS : uint8_t {
            READY = 0,        // 就绪状态
            ANALYZING = 1,    // 分析中
            REPLICATE = 2     // 复制中
        };

        /**
         * 实例角色枚举类型
         */
        enum class ROLE : uint8_t {
            NONE = 0,         // 未知角色
            PRIMARY = 1,      // 主实例
            PHYSICAL_STANDBY = 2, // 物理备用
            LOGICAL_STANDBY = 3   // 逻辑备用
        };

        // 核心组件
        Schema* schema;           // 数据库架构
        Ctx* ctx;                 // 上下文对象
        Locales* locales;         // 本地化对象
        State* state{nullptr};    // 状态管理器
        State* stateDisk{nullptr};// 磁盘状态管理器
        Serializer* serializer{nullptr}; // 序列化器
        std::atomic<STATUS> status{STATUS::READY}; // 当前复制器状态

        // 启动参数
        std::string database;     // 数据库名称
        Scn startScn;             // 起始SCN
        Seq startSequence;        // 起始序列号
        std::string startTime;    // 起始时间
        uint64_t startTimeRel;    // 相对起始时间

        // 数据库参数
        bool onlineData{false};   // 是否为在线数据
        bool suppLogDbPrimary{false}; // 是否开启主键补充日志记录
        bool suppLogDbAll{false}; // 是否开启全字段补充日志记录
        bool logArchiveFormatCustom{false}; // 是否使用自定义日志归档格式
        bool allowedCheckpoints{false}; // 是否允许检查点
        // 如果启动时数据不可用，是否用故障安全模式启动
        bool bootFailsafe{false};
        
        typeConId conId;          // 容器ID
        std::string conName;      // 容器名称
        std::string context;      // 上下文
        std::string dbTimezoneStr; // 数据库时区字符串
        int64_t dbTimezone{0};    // 数据库时区(秒偏移)
        std::string dbRecoveryFileDest; // 数据库恢复文件目录
        std::string logArchiveDest; // 日志归档目标
        std::string dbBlockChecksum; // 数据块校验和
        std::string nlsCharacterSet; // NLS字符集
        std::string logArchiveFormat{"o1_mf_%t_%s_%h_.arc"}; // 日志归档格式
        std::string nlsNcharCharacterSet; // NLS NCHAR字符集
        uint64_t defaultCharacterMapId{0}; // 默认字符映射ID
        uint64_t defaultCharacterNcharMapId{0}; // 默认NCHAR字符映射ID
        Scn firstDataScn{Scn::none()}; // 第一个数据SCN
        Scn firstSchemaScn{Scn::none()}; // 第一个架构SCN
        std::set<RedoLog*> redoLogs; // 重做日志文件集合

        // 事务架构一致性互斥锁
        std::mutex mtxTransaction;

        // 检查点信息及其互斥锁
        std::mutex mtxCheckpoint;
        typeResetlogs resetlogs{0}; // 重置日志标识
        std::set<DbIncarnation*> dbIncarnations; // 数据库incarnation集合
        DbIncarnation* dbIncarnationCurrent{nullptr}; // 当前数据库incarnation
        typeActivation activation{0}; // 激活标识
        Seq sequence{Seq::none()}; // 当前序列号
        Seq lastSequence{Seq::none()}; // 上次序列号
        FileOffset fileOffset; // 文件偏移量
        Scn firstScn{Scn::none()}; // 首个SCN
        Scn nextScn{Scn::none()}; // 下一个SCN
        Scn clientScn{Scn::none()}; // 客户端SCN
        typeIdx clientIdx{0}; // 客户端索引
        uint64_t checkpoints{0}; // 检查点计数
        Scn checkpointScn{Scn::none()}; // 检查点SCN
        Scn lastCheckpointScn{Scn::none()}; // 上次检查点SCN
        Time checkpointTime{0}; // 检查点时间
        Time lastCheckpointTime; // 上次检查点时间
        Seq checkpointSequence{Seq::none()}; // 检查点序列号
        FileOffset checkpointFileOffset; // 检查点文件偏移
        FileOffset lastCheckpointFileOffset; // 上次检查点文件偏移
        uint64_t checkpointBytes{0}; // 检查点字节数
        uint64_t lastCheckpointBytes{0}; // 上次检查点字节数
        Seq minSequence{Seq::none()}; // 最小序列号
        FileOffset minFileOffset; // 最小文件偏移
        Xid minXid; // 最小事务ID
        uint64_t schemaInterval{0}; // 架构更新间隔
        std::set<Scn> checkpointScnList; // 检查点SCN列表
        std::unordered_map<Scn, bool> checkpointSchemaMap; // 检查点架构映射

        // 新Schema元素临时存储
        std::vector<SchemaElement*> newSchemaElements;

        // Schema信息及其互斥锁
        std::mutex mtxSchema;
        std::vector<SchemaElement*> schemaElements; // Schema元素集合
        std::set<std::string> users; // 用户集合

        /**
         * 构造函数
         *
         * @param newCtx 上下文对象
         * @param newLocales 本地化对象
         * @param newDatabase 数据库名称
         * @param newConId 容器ID
         * @param newStartScn 起始SCN
         * @param newStartSequence 起始序列号
         * @param newStartTime 起始时间
         * @param newStartTimeRel 相对起始时间
         */
        Metadata(Ctx* newCtx, Locales* newLocales, std::string newDatabase, typeConId newConId, Scn newStartScn,
                 Seq newStartSequence, std::string newStartTime, uint64_t newStartTimeRel);
        
        /**
         * 析构函数
         */
        ~Metadata();

        /**
         * 设置NLS字符集
         * 
         * @param nlsCharset 字符集
         * @param nlsNcharCharset NCHAR字符集
         */
        void setNlsCharset(const std::string& nlsCharset, const std::string& nlsNcharCharset);
        
        /**
         * 清理重做日志
         */
        void purgeRedoLogs();
        
        /**
         * 设置序列号和文件偏移量
         * 
         * @param newSequence 新序列号
         * @param newFileOffset 新文件偏移量
         */
        void setSeqFileOffset(Seq newSequence, FileOffset newFileOffset);
        
        /**
         * 设置重置日志标识
         * 
         * @param newResetlogs 新重置日志标识
         */
        void setResetlogs(typeResetlogs newResetlogs);
        
        /**
         * 设置激活标识
         * 
         * @param newActivation 新激活标识
         */
        void setActivation(typeActivation newActivation);
        
        /**
         * 设置首个和下一个SCN
         * 
         * @param newFirstScn 新首个SCN
         * @param newNextScn 新下一个SCN
         */
        void setFirstNextScn(Scn newFirstScn, Scn newNextScn);
        
        /**
         * 设置下一个序列号
         */
        void setNextSequence();
        
        /**
         * 从状态存储读取数据
         * 
         * @param name 文件名
         * @param maxSize 最大大小
         * @param in 输出字符串
         * @return 是否成功
         */
        [[nodiscard]] bool stateRead(const std::string& name, uint64_t maxSize, std::string& in) const;
        
        /**
         * 从磁盘状态存储读取数据
         * 
         * @param name 文件名
         * @param maxSize 最大大小
         * @param in 输出字符串
         * @return 是否成功
         */
        [[nodiscard]] bool stateDiskRead(const std::string& name, uint64_t maxSize, std::string& in) const;
        
        /**
         * 向状态存储写入数据
         * 
         * @param name 文件名
         * @param scn SCN
         * @param out 输出流
         * @return 是否成功
         */
        [[nodiscard]] bool stateWrite(const std::string& name, Scn scn, const std::ostringstream& out) const;
        
        /**
         * 从状态存储删除文件
         * 
         * @param name 文件名
         * @return 是否成功
         */
        [[nodiscard]] bool stateDrop(const std::string& name) const;
        
        /**
         * 添加Schema元素(带两个选项)
         * 
         * @param owner 所有者
         * @param table 表名
         * @param options1 选项1
         * @param options2 选项2
         * @return 新Schema元素
         */
        SchemaElement* addElement(const std::string& owner, const std::string& table, DbTable::OPTIONS options1, DbTable::OPTIONS options2);
        
        /**
         * 添加Schema元素(带一个选项)
         * 
         * @param owner 所有者
         * @param table 表名
         * @param options 选项
         * @return 新Schema元素
         */
        SchemaElement* addElement(const std::string& owner, const std::string& table, DbTable::OPTIONS options);
        
        /**
         * 重置元素
         */
        void resetElements();
        
        /**
         * 提交元素变更
         */
        void commitElements();
        
        /**
         * 构建映射表
         * 
         * @param msgs 消息列表
         * @param tablesUpdated 已更新表映射
         */
        void buildMaps(std::vector<std::string>& msgs, std::unordered_map<typeObj, std::string>& tablesUpdated);

        /**
         * 等待写入器线程
         * 
         * @param t 线程对象
         */
        void waitForWriter(Thread* t);
        
        /**
         * 等待复制器线程
         * 
         * @param t 线程对象
         */
        void waitForReplicator(Thread* t);
        
        /**
         * 设置就绪状态
         * 
         * @param t 线程对象
         */
        void setStatusReady(Thread* t);
        
        /**
         * 设置开始状态
         * 
         * @param t 线程对象
         */
        void setStatusStart(Thread* t);
        
        /**
         * 设置复制状态
         * 
         * @param t 线程对象
         */
        void setStatusReplicate(Thread* t);
        
        /**
         * 唤醒线程
         * 
         * @param t 线程对象
         */
        void wakeUp(Thread* t);
        
        /**
         * 创建检查点
         * 
         * @param t 线程对象
         * @param newCheckpointScn 新检查点SCN
         * @param newCheckpointTime 新检查点时间
         * @param newCheckpointSequence 新检查点序列号
         * @param newCheckpointFileOffset 新检查点文件偏移
         * @param newCheckpointBytes 新检查点字节数
         * @param newMinSequence 新最小序列号
         * @param newMinFileOffset 新最小文件偏移
         * @param newMinXid 新最小事务ID
         */
        void checkpoint(Thread* t, Scn newCheckpointScn, Time newCheckpointTime, Seq newCheckpointSequence, FileOffset newCheckpointFileOffset,
                        uint64_t newCheckpointBytes, Seq newMinSequence, FileOffset newMinFileOffset, Xid newMinXid);
        
        /**
         * 写入检查点
         * 
         * @param t 线程对象
         * @param force 是否强制写入
         */
        void writeCheckpoint(Thread* t, bool force);
        
        /**
         * 读取所有检查点
         */
        void readCheckpoints();
        
        /**
         * 读取指定SCN的检查点
         * 
         * @param scn SCN
         */
        void readCheckpoint(Scn scn);
        
        /**
         * 删除旧检查点
         * 
         * @param t 线程对象
         */
        void deleteOldCheckpoints(Thread* t);
        
        /**
         * 加载自适应Schema
         */
        void loadAdaptiveSchema();
        
        /**
         * 允许创建检查点
         */
        void allowCheckpoints();
        
        /**
         * 检查是否为新数据
         * 
         * @param scn SCN
         * @param idx 索引
         * @return 是否为新数据
         */
        bool isNewData(Scn scn, typeIdx idx) const;
    };
}

#endif
