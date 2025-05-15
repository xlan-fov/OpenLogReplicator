/* 数据库连接类头文件
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

#ifdef LINK_LIBRARY_OCI
#include <oci.h>

namespace OpenLogReplicator {
    // 前向声明
    class Ctx;
    class DatabaseEnvironment;
    struct DbReturn;

    // 数据库连接类 - 管理Oracle数据库连接
    class DatabaseConnection final {
    protected:
        Ctx* ctx;                       // 上下文对象
        DatabaseEnvironment* env;       // 数据库环境
        bool connected;                 // 是否已连接
        OCIError* errorHandle;          // OCI错误句柄
        OCIServer* serverHandle;        // OCI服务器句柄
        OCISvcCtx* serviceContextHandle; // OCI服务上下文句柄
        OCISession* sessionHandle;      // OCI会话句柄
        OCIStmt* statementHandle;       // OCI语句句柄
        OCIStmt* statementHandle2;      // OCI语句句柄2
        std::vector<OCIStmt*> statementHandles; // OCI语句句柄集合
        std::string sid;                // 会话ID
        bool tracing;                   // 是否开启追踪

        // OCI错误处理方法
        static bool checkError(const char* call, OCIError* err, sword status);
        bool checkError(const char* call, sword status) const;
        [[nodiscard]] std::string getSqlError() const;
        std::unordered_map<std::string, OCIStmt*> preparedStmts; // 预编译语句集合

    public:
        enum class CONNECTION_TYPE : unsigned char {
            NONE = 0,                  // 无类型
            DIRECT = 1,                // 直连
            WALLET = 2,                // 钱包
            PROXY = 3,                 // 代理
            ADMIN = 4                  // 管理员
        };

        // 构造与析构函数
        DatabaseConnection(Ctx* newCtx, DatabaseEnvironment* newEnv);
        ~DatabaseConnection();

        // 连接管理方法
        bool connect(const std::string& username, const std::string& password, const std::string& server, CONNECTION_TYPE connectionType);
        bool connectAdmin(const std::string& username, const std::string& password, const std::string& server);
        bool disconnect();
        bool isConnected() const;
        
        // SQL查询执行方法
        void disableTracing();
        bool execute(const std::string& sql);
        DbReturn execute2(const std::string& sql);
        
        // 参数绑定和执行
        bool execute(const std::string& sql, int p1);
        bool execute(const std::string& sql, int64_t p1);
        bool execute(const std::string& sql, const std::string& p1);
        bool execute(const std::string& sql, int p1, int p2);
        bool execute(const std::string& sql, int p1, int64_t p2);
        bool execute(const std::string& sql, int64_t p1, int64_t p2);
        bool execute(const std::string& sql, int p1, const std::string& p2);
        bool execute(const std::string& sql, const std::string& p1, int p2);
        bool execute(const std::string& sql, const std::string& p1, int64_t p2);
        bool execute(const std::string& sql, const std::string& p1, const std::string& p2);
        bool execute(const std::string& sql, int p1, int p2, int p3);
        bool execute(const std::string& sql, int p1, int p2, const std::string& p3);
        bool execute(const std::string& sql, int p1, int p2, int p3, int p4, int p5, int p6, int p7, int p8, int p9, int p10);
        
        // 参数绑定和查询执行
        DbReturn query(const std::string& sql);
        DbReturn query(const std::string& sql, int p1);
        DbReturn query(const std::string& sql, int64_t p1);
        DbReturn query(const std::string& sql, const std::string& p1);
        DbReturn query(const std::string& sql, int p1, int p2);
        DbReturn query(const std::string& sql, int p1, int64_t p2);
        DbReturn query(const std::string& sql, int64_t p1, int64_t p2);
        DbReturn query(const std::string& sql, int p1, const std::string& p2);
        DbReturn query(const std::string& sql, const std::string& p1, int p2);
        DbReturn query(const std::string& sql, const std::string& p1, int64_t p2);
        DbReturn query(const std::string& sql, const std::string& p1, const std::string& p2);
        DbReturn query(const std::string& sql, int p1, int p2, int p3);
        DbReturn query(const std::string& sql, int p1, int p2, const std::string& p3);
        
        // OCI句柄操作
        OCIStmt* allocateStatement();
        void deleteStatement(OCIStmt* stmt);
        OCIStmt* prepare(OCIStmt* stmt, const std::string& sql, const std::string& key);
        
        // 会话ID获取
        [[nodiscard]] std::string getSid() const;
    };
}
#endif /* LINK_LIBRARY_OCI */