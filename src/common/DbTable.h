/**
 * 数据库表类头文件
 * 
 * 该文件定义了DbTable类，表示Oracle数据库中的表对象。
 * DbTable存储表的结构信息，包括列、LOB对象、分区等。
 *
 * @file DbTable.h
 * @author Adam Leszczynski (aleszczynski@bersler.com)
 * @copyright Copyright (C) 2018-2025 Adam Leszczynski
 * @license GPL-3.0
 */

#ifndef DB_TABLE_H_
#define DB_TABLE_H_

#include <unordered_map>
#include <vector>

#include "expression/Token.h"
#include "types/Types.h"

namespace OpenLogReplicator {
    class BoolValue;
    class Ctx;
    class DbColumn;
    class DbLob;
    class Expression;
    class Token;

    /**
     * 数据库表类 - 表示Oracle数据库中的表对象
     */
    class DbTable final {
    public:
        /**
         * 表选项枚举类型
         */
        enum class OPTIONS : unsigned char {
            DEFAULT = 0,         // 默认选项
            DEBUG_TABLE = 1 << 0, // 调试表
            SYSTEM_TABLE = 1 << 1, // 系统表
            SCHEMA_TABLE = 1 << 2  // 架构表
        };
        
        /**
         * 系统表类型枚举
         */
        enum class TABLE : unsigned char {
            NONE, SYS_CCOL, SYS_CDEF, SYS_COL, SYS_DEFERRED_STG, SYS_ECOL, SYS_LOB, SYS_LOB_COMP_PART, SYS_LOB_FRAG, SYS_OBJ, SYS_TAB, SYS_TABPART,
            SYS_TABCOMPART, SYS_TABSUBPART, SYS_TS, SYS_USER, XDB_TTSET, XDB_XNM, XDB_XPT, XDB_XQN
        };

        /** 上下文长度常量 */
        static constexpr uint VCONTEXT_LENGTH{30};
        /** 参数长度常量 */
        static constexpr uint VPARAMETER_LENGTH{4000};
        /** 属性长度常量 */
        static constexpr uint VPROPERTY_LENGTH{4000};

        typeObj obj;           // 对象ID
        typeDataObj dataObj;   // 数据对象ID
        typeUser user;
        typeCol cluCols;
        typeCol totalPk{0};
        typeCol totalLobs{0};
        OPTIONS options;
        typeCol maxSegCol{0};
        typeCol guardSegNo{-1};
        std::string owner;
        std::string name;
        std::string tokSuf;
        std::string condition;
        BoolValue* conditionValue{nullptr};
        std::vector<DbColumn*> columns;
        std::vector<DbLob*> lobs;
        std::vector<typeObj2> tablePartitions;
        std::vector<typeCol> pk;
        std::vector<typeCol> tagCols;
        std::vector<Token*> tokens;
        std::vector<Expression*> stack;
        TABLE systemTable;
        bool sys;

        DbTable(typeObj newObj, typeDataObj newDataObj, typeUser newUser, typeCol newCluCols, OPTIONS newOptions, std::string newOwner,
                std::string newName);
        ~DbTable();

        void addColumn(DbColumn* column);
        void addLob(DbLob* lob);
        void addTablePartition(typeObj newObj, typeDataObj newDataObj);
        bool matchesCondition(const Ctx* ctx, char op, const std::unordered_map<std::string, std::string>* attributes) const;
        void setCondition(const std::string& newCondition);

        static bool isDebugTable(OPTIONS options) {
            return (static_cast<uint>(options) & static_cast<uint>(OPTIONS::DEBUG_TABLE)) != 0;
        }

        static bool isSchemaTable(OPTIONS options) {
            return (static_cast<uint>(options) & static_cast<uint>(OPTIONS::SCHEMA_TABLE)) != 0;
        }

        static bool isSystemTable(OPTIONS options) {
            return (static_cast<uint>(options) & static_cast<uint>(OPTIONS::SYSTEM_TABLE)) != 0;
        }

        friend std::ostream& operator<<(std::ostream& os, const DbTable& table);
    };
}

#endif
