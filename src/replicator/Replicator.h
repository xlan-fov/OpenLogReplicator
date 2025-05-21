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
         * 此方法用于添加源路径和目标路径的映射关系，便于系统在不同环境中处理文件路径。
         * 当系统需要查找文件时，会根据这些映射规则将源路径转换为目标路径。
         * 
         * @param source 源路径 - 原始的文件路径
         * @param target 目标路径 - 映射后的文件路径
         */
        void addPathMapping(std::string source, std::string target);
        
        /**
         * 添加重做日志批次
         * 
         * 将指定的重做日志文件路径添加到批处理列表中，这些日志将按顺序处理。
         * 批处理模式适用于离线分析或特定顺序的日志处理场景。
         * 
         * @param path 重做日志路径 - 要添加到批处理队列的日志文件路径
         */
        void addRedoLogsBatch(std::string path);
        
        /**
         * 从路径获取归档日志
         * 
         * 该方法从文件系统路径中扫描并获取归档日志文件。系统会根据配置的路径映射
         * 和日志格式，识别符合条件的归档日志文件，并将它们添加到处理队列中。
         * 这种方式适用于从本地文件系统或挂载的网络存储中读取归档日志。
         * 
         * @param replicator 复制器对象 - 用于访问复制器的状态和配置
         */
        static void archGetLogPath(Replicator* replicator);
        
        /**
         * 从列表获取归档日志
         * 
         * 该方法从预定义的列表中获取归档日志文件，而不是从文件系统路径扫描。
         * 这适用于已知确切的日志文件列表，或者需要按特定顺序处理日志的场景。
         * 系统将按照列表中的顺序处理这些日志文件。
         * 
         * @param replicator 复制器对象 - 用于访问复制器的状态和配置
         */
        static void archGetLogList(Replicator* replicator);
        
        /**
         * 应用路径映射规则
         * 
         * 将路径映射规则应用到给定的文件路径上，根据配置的映射关系
         * 转换路径。这使得系统能够处理不同环境间的路径差异。
         * 
         * @param path 输入/输出参数，包含要转换的路径，函数执行后会被更新为映射后的路径
         */
        void applyMapping(std::string& path);
        
        /**
         * 更新重置日志信息
         * 
         * 更新数据库重置日志(resetlogs)信息，这在数据库重置或恢复操作后非常重要。
         * 系统需要跟踪这些信息以正确处理日志序列变化。
         */
        void updateResetlogs();
        
        /**
         * 唤醒复制器线程
         * 
         * 唤醒处于等待状态的复制器线程，使其继续处理工作。
         * 此方法通常由其他线程调用以触发复制器的活动。
         */
        void wakeUp() override;
        
        /**
         * 打印启动消息
         * 
         * 显示复制器启动时的相关信息，包括模式、配置参数等。
         * 这有助于用户了解复制器的当前工作状态。
         */
        void printStartMsg() const;
        
        /**
         * 处理归档重做日志
         * 
         * 处理已归档的重做日志文件。系统会按照特定顺序读取和分析这些日志，
         * 并根据其内容更新数据库状态。
         * 
         * @return 如果成功处理了一个或多个日志文件返回true，否则返回false
         */
        bool processArchivedRedoLogs();
        
        /**
         * 处理在线重做日志
         * 
         * 处理当前活动的(在线)重做日志文件。这些日志正在被数据库系统写入，
         * 复制器会实时读取并处理这些变更。
         * 
         * @return 如果成功处理了一个或多个在线日志返回true，否则返回false
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
