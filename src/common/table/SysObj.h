/**
 * SYS.OBJ$表定义
 * 
 * 该文件定义了SysObj类，映射Oracle数据库中的SYS.OBJ$表结构。
 * SYS.OBJ$表存储了数据库中所有对象的基本信息。
 *
 * @file SysObj.h
 * @author Adam Leszczynski (aleszczynski@bersler.com)
 * @copyright Copyright (C) 2018-2025 Adam Leszczynski
 * @license GPL-3.0
 */

#ifndef SYS_OBJ_H_
#define SYS_OBJ_H_

#include "../types/IntX.h"
#include "../types/RowId.h"
#include "../types/Types.h"

namespace OpenLogReplicator {
    /**
     * SYS.OBJ$表类 - 存储数据库对象信息
     */
    class SysObj final {
    public:
        /**
         * 对象标志枚举类型
         */
        enum class FLAGS : unsigned long long {
            NONE = 0,
            FDOM = 1ULL << 0, TEMPORARY = 1ULL << 1, SYSTEM_GENERATED = 1ULL << 2, UNBOUND = 1ULL << 3, SECONDARY = 1ULL << 4, IN_MEMORY_TEMP = 1ULL << 5,
            PERMANENTLY_KEPT_JAVA_CLASS = 1ULL << 6, DROPPED = 1ULL << 7, SYNONYM_HAS_VPD_POLICIES = 1ULL << 8, SYNONYM_HAS_VPD_GROUPS = 1ULL << 9,
            SYNONYM_HAS_VPD_CONTEXT = 1ULL << 10, CURSOR_DURATION = 1ULL << 11, DEPENDENCY_TYPE_EVOLVED = 1ULL << 12, DISABLE_FAST_VALIDATION = 1ULL << 13,
            NESTED_TABLE_PARTITION = 1ULL << 14, OBJERROR_ROW = 1ULL << 15, METADATA_LINK = 1ULL << 16, OBJECT_LINK = 1ULL << 17, LONG_IDENTIFIER = 1ULL << 18,
            ALLOW_FAST_ALTER_TABLE_UPGRADE = 1ULL << 19, NOT_EDITIONABLE = 1ULL << 20, SPECIAL_INVOKER_RIGHTS = 1ULL << 21, DATABASE_SUPPLIED_OBJECT = 1ULL << 22,
            NO_FINE_GRAINED_DEP = 1ULL << 23, COMMON_OBJECT_MISMATCH = 1ULL << 24, LOCAL_MCODE = 1ULL << 25, LOCAL_DIANA = 1ULL << 26,
            FEDERATION_OBJECT = 1ULL << 27, DEFAULT_COLLATION = 1ULL << 28, ON_ALL_SHARDS = 1ULL << 29, SHARDED = 1ULL << 30, REFERENCE = 1ULL << 31,
            EXTENDED_DATA_LINK = 1ULL << 32, BINARY_COLLATION = 1ULL << 32, DISABLE_LOG_REPLICATION = 1ULL << 34
        };

        /** 对象名称最大长度 */
        static constexpr uint NAME_LENGTH = 128;

        /**
         * 对象类型枚举类型
         */
        enum class OBJTYPE : unsigned char {
            // 11.2
            NEXT_OBJECT = 0, INDEX = 1, TABLE = 2, CLUSTER = 3, VIEW = 4, SYNONYM = 5, SEQUENCE = 6, PROCEDURE = 7, FUNCTION = 8, PACKAGE = 9,
            NON_EXISTENT = 10, PACKAGE_BODY = 11, TRIGGER = 12, TYPE = 13, TYPE_BODY = 14, VARCHAR_STORED_LOB = 21, LIBRARY = 22, JAVA_SOURCE = 28,
            JAVA_CLASS = 29, INDEXTYPE = 32, OPERATOR = 33, LOB = 40, MATERIALIZED_VIEW = 42, DIMENSION = 43, RULE_SET = 46, XML_SCHEMA = 55,
            JAVA_DATA = 56, RULE = 59, EVALUATION_CONTEXT = 62, ASSEMBLY = 87, CREDENTIAL = 90, CUBE_DIMENSION = 92, CUBE = 93, MEASURE_FOLDER = 94,
            CUBE_BUILD_PROCESS = 95,
            // 12.1
            DIRECTORY = 23,
            // 12.2
            HIERARCHY = 150, ATTRIBUTE_DIMENSION = 151, ANALYTIC_VIEW = 152,
            // 19.0
            QUEUE = 24
        };

        RowId rowId;          // 行ID
        typeUser owner{0};    // 所有者ID
        typeObj obj{0};       // 对象ID
        typeDataObj dataObj{0}; // 数据对象ID
        OBJTYPE type{OBJTYPE::NEXT_OBJECT}; // 对象类型
        std::string name;     // 对象名称
        IntX flags{0, 0};     // 标志位
        bool single{false};   // 是否为单一对象

        SysObj(RowId newRowId, typeUser newOwner, typeObj newObj, typeDataObj newDataObj, OBJTYPE newType, std::string newName, uint64_t newFlags1,
               uint64_t newFlags2, bool newSingle) :
                rowId(newRowId),
                owner(newOwner),
                obj(newObj),
                dataObj(newDataObj),
                type(newType),
                name(std::move(newName)),
                flags(newFlags1, newFlags2),
                single(newSingle) {
        }

        explicit SysObj(RowId newRowId) :
                rowId(newRowId) {
        }

        bool operator!=(const SysObj& other) const {
            return (other.rowId != rowId) || (other.owner != owner) || (other.obj != obj) || (other.dataObj != dataObj) || (other.type != type) ||
                   (other.name != name) || (other.flags != flags);
        }

        [[nodiscard]] bool isLob() const {
            return (type == OBJTYPE::LOB || type == OBJTYPE::VARCHAR_STORED_LOB);
        }

        [[nodiscard]] bool isTable() const {
            return (type == OBJTYPE::TABLE);
        }

        [[nodiscard]] bool isFlags(FLAGS val) const{
            return flags.isSet64(static_cast<uint64_t>(val));
        }

        [[nodiscard]] bool isTemporary() const {
            return isFlags(FLAGS::TEMPORARY) ||
                   isFlags(FLAGS::SECONDARY) ||
                   isFlags(FLAGS::IN_MEMORY_TEMP);
        }

        [[nodiscard]] bool isDropped() const {
            return isFlags(FLAGS::DROPPED);
        }

        [[nodiscard]] static std::string tableName() {
            return "SYS.OBJ$";
        }

        [[nodiscard]] std::string toString() const {
            return "ROWID: " + rowId.toString() + ", OWNER#: " + std::to_string(owner) + ", OBJ#: " + std::to_string(obj) + ", DATAOBJ#: " +
                   std::to_string(dataObj) + ", TYPE#: " + std::to_string(static_cast<uint>(type)) + ", NAME: '" + name + "', FLAGS: " + flags.toString();
        }

        [[nodiscard]] static constexpr bool dependentTable() {
            return true;
        }

        [[nodiscard]] static constexpr bool dependentTableLob() {
            return false;
        }

        [[nodiscard]] static constexpr bool dependentTableLobFrag() {
            return false;
        }

        [[nodiscard]] static constexpr bool dependentTablePart() {
            return false;
        }

        [[nodiscard]] typeObj getDependentTable() const {
            return obj;
        }
    };

    class SysObjNameKey final {
    public:
        typeUser owner;
        std::string name;
        typeObj obj;
        typeDataObj dataObj;

        SysObjNameKey(typeUser newOwner, std::string newName, typeObj newObj, typeDataObj newDataObj) :
                owner(newOwner),
                name(std::move(newName)),
                obj(newObj),
                dataObj(newDataObj) {
        }

        explicit SysObjNameKey(const SysObj* sysObj) :
                owner(sysObj->owner),
                name(sysObj->name),
                obj(sysObj->obj),
                dataObj(sysObj->dataObj) {
        }

        /**
         * SysObjNameKey类比较运算符
         */
        bool operator<(const SysObjNameKey& other) const {
            if (owner < other.owner)
                return true;
            if (other.owner < owner)
                return false;
            const int cmp = other.name.compare(name);
            if (0 < cmp)
                return true;
            if (cmp < 0)
                return false;
            if (obj < other.obj)
                return true;
            if (other.obj < obj)
                return false;
            if (dataObj < other.dataObj)
                return true;
            if (other.dataObj < dataObj)
                return false;
            return false;
        }
    };

    class SysObjObj final {
    public:
        typeObj obj;

        explicit SysObjObj(typeObj newObj) :
                obj(newObj) {
        }

        explicit SysObjObj(const SysObj* sysObj) :
                obj(sysObj->obj) {
        }

        bool operator!=(const SysObjObj other) const {
            return (other.obj != obj);
        }

        bool operator==(const SysObjObj other) const {
            return (other.obj == obj);
        }
    };
}

namespace std {
    template<>
    struct hash<OpenLogReplicator::SysObjObj> {
        size_t operator()(const OpenLogReplicator::SysObjObj sysObjObj) const {
            return hash<typeObj>()(sysObjObj.obj);
        }
    };
}

#endif
