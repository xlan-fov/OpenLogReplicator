/**
 * 复制器类头文件
 * 
 * 该文件定义了Replicator基类，用于读取和解析数据库重做日志。
 * Replicator负责管理重做日志的读取、解析和处理，实现数据复制功能。
 *
 * @file Replicator.h
 * @author Adam Leszczynski (aleszczynski@...
 * @license GPL-3.0
 */

#ifndef REPLICATOR_H_
#define REPLICATOR_H_

#include <fstream>
#include <queue>
#include <set>
#include <unordered_map>
#include <vector>

#include "../common/Ctx.h"
#include "../common/RedoLogRecord.h"
#include "../common/Thread.h"
#include "../common/exception/RedoLogException.h"

namespace OpenLogReplicator {
    class Parser;
    class Builder;
    class Metadata;
    class Reader;
    class RedoLogRecord;
    class State;
    class Transaction;
    class TransactionBuffer;

    /**
     * 解析器比较结构体 - 用于排序重做日志解析器
     */
    struct parserCompare {
        /**
         * 比较两个解析器的序列号
         * 
         * @param p1 第一个解析器指针
         * @param p2 第二个解析器指针
         * @return 如果p1的序列号大于p2的序列号，则返回true
         */
        bool operator()(const Parser* p1, const Parser* p2);
    };

    /**
     * 复制器类 - 负责重做日志的读取和解析
     */
    class Replicator : public Thread {
    protected:
        void (* archGetLog)(Replicator* replicator); // 获取归档日志的函数指针
        Builder* builder;                  // 构建器对象
        Metadata* metadata;                // 元数据对象
        TransactionBuffer* transactionBuffer; // 事务缓冲区
        std::string database;              // 数据库名称
        std::string redoCopyPath;          // 重做日志复制路径
        
        // 重做日志文件
        Reader* archReader{nullptr};       // 归档重做日志读取器
        std::string lastCheckedDay;        // 最后检查的日期
        std::priority_queue<Parser*, std::vector<Parser*>, parserCompare> archiveRedoQueue; // 归档重做日志队列
        std::set<Parser*> onlineRedoSet;   // 在线重做日志集合
        std::set<Reader*> readers;         // 读取器集合
        std::vector<std::string> pathMapping; // 路径映射
        std::vector<std::string> redoLogsBatch; // 重做日志批次

        /**
         * 清理归档列表
         */
        void cleanArchList();
        
        /**
         * 更新在线日志状态
         */
        void updateOnlineLogs();
        
        /**
         * 停止并清理所有读取器
         */
        void readerDropAll();
        
        /**
         * 从文件名获取序列号
         * 
         * @param replicator 复制器对象
         * @param file 文件名
         * @return 序列号
         */
        static Seq getSequenceFromFileName(Replicator* replicator, const std::string& file);
        
        /**
         * 获取模式名称
         * 
         * @return 模式名称
         */
        virtual std::string getModeName() const;
        
        /**
         * 检查数据库连接
         * 
         * @return 如果连接正常则返回true
         */
        virtual bool checkConnection();
        
        /**
         * 检查是否继续使用在线模式
         * 
         * @return 如果应该继续使用在线模式则返回true
         */
        virtual bool continueWithOnline();
        
        /**
         * 验证当前Schema
         * 
         * @param currentScn 当前SCN
         */
        virtual void verifySchema(Scn currentScn);
        
        /**
         * 创建Schema
         */
        virtual void createSchema();
        
        /**
         * 更新在线重做日志数据
         */
        virtual void updateOnlineRedoLogData();

    public:
        /**
         * 构造函数
         * 
         * @param newCtx 上下文对象
         * @param newArchGetLog 获取归档日志的函数指针
         * @param newBuilder 构建器对象
         * @param newMetadata 元数据对象
         * @param newTransactionBuffer 事务缓冲区
         * @param newAlias 别名
         * @param newDatabase 数据库名称
         */
        Replicator(Ctx* newCtx, void (* newArchGetLog)(Replicator* replicator), Builder* newBuilder, Metadata* newMetadata,
                   TransactionBuffer* newTransactionBuffer, std::string newAlias, std::string newDatabase);
        
        /**
         * 析构函数
         */
        ~Replicator() override;

        /**
         * 初始化复制器
         */
        virtual void initialize();
        
        /**
         * 定位读取器位置
         */
        virtual void positionReader();
        
        /**
         * 加载数据库元数据
         */
        virtual void loadDatabaseMetadata();
        
        /**
         * 线程主函数
         */
        void run() override;
        
        /**
         * 创建读取器
         * 
         * @param group 重做日志组号
         * @return 新创建的读取器
         */
        virtual Reader* readerCreate(int group);
        
        /**
         * 检查在线重做日志
         */
        void checkOnlineRedoLogs();
        
        /**
         * 转为备用模式
         */
        virtual void goStandby();
        
        /**
         * 添加路径映射
         * 
         * @param source 源路径
         * @param target 目标路径
         */
        void addPathMapping(std::string source, std::string target);
        
        /**
         * 添加重做日志批次
         * 
         * @param path 重做日志路径
         */
        void addRedoLogsBatch(std::string path);
        
        /**
         * 从路径获取归档日志
         * 
         * @param replicator 复制器对象
         */
        static void archGetLogPath(Replicator* replicator);
        
        /**
         * 从列表获取归档日志
         * 
         * @param replicator 复制器对象
         */
        static void archGetLogList(Replicator* replicator);
        
        /**
         * 应用路径映射
         * 
         * @param path 路径引用
         */
        void applyMapping(std::string& path);
        
        /**
         * 更新重置日志信息
         */
        void updateResetlogs();
        
        /**
         * 唤醒线程
         */
        void wakeUp() override;
        
        /**
         * 打印启动信息
         */
        void printStartMsg() const;
        
        /**
         * 处理归档重做日志
         * 
         * @return 如果处理了日志则返回true
         */
        bool processArchivedRedoLogs();
        
        /**
         * 处理在线重做日志
         * 
         * @return 如果处理了日志则返回true
         */
        bool processOnlineRedoLogs();

        /**
         * 获取线程名称
         * 
         * @return 线程名称
         */
        std::string getName() const override {
            return {"Replicator: " + alias};
        }
    };
}

#endif
