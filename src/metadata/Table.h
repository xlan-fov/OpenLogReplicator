/* 表对象类头文件
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

#pragma once

#include <string>
#include <vector>
#include <deque>
#include "SchemaElement.h"
#include "Column.h"
#include "RedoLogRecord.h"
#include "TransactionBuffer.h"

namespace OpenLogReplicator {
    // 前向声明
    class Ctx;

    // 表类 - 表示数据库中的表对象结构
    class Table : public SchemaElement {
    public:
        // 表选项标志
        enum class OPTIONS : uint64_t {
            NONE = 0,                 // 无选项
            HIDDEN_COLUMNS = 1,       // 隐藏列
            GUARD_COLUMNS = 2,        // 保护列
            NESTED_TABLES = 4,        // 嵌套表
            UNUSED_COLUMNS = 8,       // 未使用列
            ADD = 16,                 // 添加
            KEEP = 32,                // 保留
            SET_KEY_LIST = 64,        // 设置键列表
            SET_TAG_LIST = 128,       // 设置标签列表
            SYSTEM_TABLE = 256,       // 系统表
            SKIP_CHECKS = 512,        // 跳过检查
            SKIP_MISSING_NOT_NULL = 1024, // 跳过缺失的非空检查
            KEEP_RED_REDO = 2048,     // 保留重做日志
            SKIP_SUPPLEMENTAL_LOG = 4096 // 跳过补充日志检查
        };

        std::string owner;             // 所有者名称
        std::string name;              // 表名
        std::string key;               // 主键定义
        TAG_TYPE tagType;              // 标签类型
        std::string tag;               // 标签定义
        std::string condition;         // 条件
        std::vector<std::string> keyList; // 键列表
        std::vector<std::string> tagList; // 标签列表
        std::deque<Column*> columns;   // 列集合
        typeObj objId;                 // 对象ID
        typeDataObj dataObj;           // 数据对象ID
        typeTs ts;                     // 表空间ID
        typeCol clucols;               // 簇列数
        typeUser owner_num;            // 所有者ID
        uint64_t flags;                // 标志位
        uint64_t property;             // 属性
        uint64_t options;              // 选项
        bool initialized;              // 是否初始化
        bool initialized2;             // 是否二次初始化
        bool binaryTag;                // 是否二进制标签
        bool partitioned;              // 是否分区
        bool nested;                   // 是否嵌套
        bool clustered;                // 是否聚簇
        bool iot;                      // 是否索引组织表
        bool dependencies;             // 是否依赖
        bool binary;                   // 是否二进制
        bool rowMovement;              // 是否行移动
        bool initial;                  // 是否初始表

        // 构造与析构函数
        Table(Ctx* newCtx, typeObj newObj);
        ~Table() override;

        // 表操作方法
        void close() override;
        void clear();
        void addSysLobFrag(typeObj fragObj, typeObj parentObj, typeTs ts);
        void addColumn(Column* column);
        void addSysCol(typeCol col, typeCol segcol, typeCol intcol, const std::string& name, typeCol2 col2, uint32_t length, uint32_t precision, int32_t scale, 
                       uint16_t charsetform, uint16_t charsetid, bool nullable, uint64_t property1, uint64_t property2);
        void addSysCCol(typeConId conId, typeCol intcol, uint64_t flags1, uint64_t flags2);
        void addSysLob(typeCol col, typeCol intcol, typeObj lObj, typeTs ts);
        void addSysLobCompPart(typeObj partObj, typeObj lObj);
        void addSysECol(typeCol column, typeGuard guardId);
        void addSysDeferredStg(uint64_t flags1, uint64_t flags2);

        // 表分区管理
        void addSysTabComPart(typeObj objId, typeDataObj dataObj, typeObj baseObject);
        void addSysTabPart(typeObj objId, typeDataObj dataObj, typeObj baseObject);
        void addSysTabSubPart(typeObj objId, typeDataObj dataObj, typeObj partitionObject);

        // 配置方法
        void setHiddenColumns(bool value);
        void setGuardColumns(bool value);
        void setNestedTables(bool value);
        void setUnusedColumns(bool value);
        void setAdd(bool value);
        void setKeep(bool value);
        void setSetKeyList(bool value);
        void setSetTagList(bool value);
        void setSystemTable(bool value);
        void setSkipChecks(bool value);
        void setSkipMissingNotNull(bool value);
        void setKeepRedRedo(bool value);
        void setSkipSupplementalLog(bool value);
        void setKeyList(const std::vector<std::string>& newKeyList);
        void setTagList(const std::vector<std::string>& newTagList);

        // 状态查询方法
        [[nodiscard]] bool isOption(OPTIONS option) const;
        [[nodiscard]] std::string getColumnName(typeCol intcol) const;
        [[nodiscard]] Column* getColumnByIntCol(typeCol intcol) const;
        [[nodiscard]] bool needSupplementalLog() const;
        [[nodiscard]] std::string toString() const override;
        [[nodiscard]] static bool isOption(uint64_t opt, OPTIONS option);

        // 数据解析方法
        void analyzeDelete(TransactionBuffer* transactionBuffer, RedoLogRecord* redoLogRecord, typeRowId* rowId, bool isKtbUndo) const;
        void analyzeInsert(TransactionBuffer* transactionBuffer, RedoLogRecord* redoLogRecord, typeRowId* rowId, uint8_t fbFlags, bool isKtbUndo) const;
        void analyzeUpdate(TransactionBuffer* transactionBuffer, RedoLogRecord* redoLogRecord, typeRowId* rowId, uint8_t fbFlags, bool isKtbUndo) const;
        void analyzeMultiInsert(TransactionBuffer* transactionBuffer, RedoLogRecord* redoLogRecord, bool isKtbUndo) const;
        void analyzeMultiDelete(TransactionBuffer* transactionBuffer, RedoLogRecord* redoLogRecord, bool isKtbUndo) const;
    };
}