/* 数据库模式管理类头文件
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

#ifndef SCHEMA_H_
#define SCHEMA_H_

#include <list>
#include <map>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <set>
#include <unordered_map>
#include <vector>

#include "../common/table/SysCCol.h"
#include "../common/table/SysCDef.h"
#include "../common/table/SysCol.h"
#include "../common/table/SysDeferredStg.h"
#include "../common/table/SysECol.h"
#include "../common/table/SysLob.h"
#include "../common/table/SysLobCompPart.h"
#include "../common/table/SysLobFrag.h"
#include "../common/table/SysObj.h"
#include "../common/table/SysTab.h"
#include "../common/table/SysTabComPart.h"
#include "../common/table/SysTabPart.h"
#include "../common/table/SysTabSubPart.h"
#include "../common/table/SysTs.h"
#include "../common/table/SysUser.h"
#include "../common/table/TablePack.h"
#include "../common/table/XdbTtSet.h"
#include "../common/table/XdbXNm.h"
#include "../common/table/XdbXPt.h"
#include "../common/table/XdbXQn.h"
#include "../common/types/RowId.h"
#include "../common/types/Types.h"
#include "../common/types/Xid.h"
#include "SchemaElement.h"

namespace OpenLogReplicator {
    // 前向声明
    class Ctx;
    class DbDefine;
    class Table;
    class SchemaElement;
    class XmlCtx;

    // 数据库模式管理类 - 管理表、列、索引等数据库对象结构
    class Schema {
    protected:
        Ctx* ctx;           // 上下文对象
        std::mutex mtx;     // 互斥锁
        std::map<uint64_t, SchemaElement*> rowIds; // 行ID到模式元素的映射

    public:
        XmlCtx* xmlCtx;     // XML上下文

        // 系统表信息
        std::unordered_map<typeDba, typeObj> dbaToObj; // 数据块地址到对象ID映射
        std::unordered_map<typeTs, std::string> tsMap; // 表空间ID到名称映射
        std::unordered_map<typeUser, std::string> userMap; // 用户ID到名称映射
        std::unordered_map<std::string, typeUser> userMapByName; // 用户名到ID映射
        std::unordered_map<typeObj, Table*> tablesMap; // 对象ID到表的映射
        std::unordered_map<typeObj, DbDefine*> defineMap; // 对象ID到约束定义的映射
        
        // XDb映射 (用于处理XML类型数据)
        std::unordered_map<std::string, std::string> xdbXptMap; // XDB_XPT映射
        std::unordered_map<std::string, std::string> xdbXPtMap2; // XDB_XPT映射2
        std::unordered_map<std::string, std::string> xdbXNmMap; // XDB_XNM映射

        // 模式版本信息
        Scn scn;           // 当前SCN
        Time time;         // 当前时间

        // 构造与析构函数
        explicit Schema(Ctx* newCtx);
        virtual ~Schema();

        // 模式对象管理方法
        void clear();
        void dumpSchema();
        void updateXmlCtx();
        void addDbDefine(DbDefine* dbDefine);

        // 表管理方法
        void drop(Table* table);
        void dropUser(typeUser user);
        void dropSchema();
        void verifyMap() const;
        Table* getTable(typeObj obj) const;
        bool hasTable(typeObj obj) const;
        void addTable(Table* table);
        DbDefine* getDbDefine(typeObj obj) const;
        Table* findTable(const std::string& owner, const std::string& table);

        // 数据库对象解析方法
        void addSysTs(void* newRowId, typeTs ts, const std::string& name, uint32_t blockSize);
        void addSysUser(void* newRowId, typeUser user, const std::string& name, uint64_t flags1, uint64_t flags2);
        void addXdbTtSet(void* newRowId, const std::string& guid, uint16_t toksuf, uint32_t flags, typeObj objId);
        void addXdbXpt(void* newRowId, const std::string& path, const std::string& id);
        void addXdbXnm(void* newRowId, const std::string& nmspace, const std::string& id);
        void addTsPrecheck(typeTs ts);
        void addUserPrecheck(typeUser user);
        void addSysObj(void* newRowId, typeUser owner, typeObj objId, typeDataObj dataObj, const std::string& name, typeObj2 object2, uint64_t flags1, uint64_t flags2);
        void addSysTab(void* newRowId, typeObj objId, typeDataObj dataObj, typeTs ts, typeCol clucols, uint64_t flags1, uint64_t flags2, uint64_t property1, uint64_t property2);
        void addSysTabComPart(void* newRowId, typeObj objId, typeDataObj dataObj, typeObj baseObject);
        void addSysTabPart(void* newRowId, typeObj objId, typeDataObj dataObj, typeObj baseObject);
        void addSysTabSubPart(void* newRowId, typeObj objId, typeDataObj dataObj, typeObj partitionObject);
        void addSysCol(void* newRowId, typeObj objId, typeCol col, typeCol segcol, typeCol intcol, const std::string& name, typeCol2 col2, uint32_t length, uint32_t precision, int32_t scale, uint16_t charsetform, uint16_t charsetid, bool nullable, uint64_t property1, uint64_t property2);
        void addSysCCol(void* newRowId, typeConId conId, typeCol intcol, typeObj objId, uint64_t flags1, uint64_t flags2);
        void addSysCDef(void* newRowId, typeConId conId, typeObj objId, uint16_t type);
        void addSysDeferredStg(void* newRowId, typeObj objId, uint64_t flags1, uint64_t flags2);
        void addSysECol(void* newRowId, typeObj tabObj, typeCol column, typeGuard guardId);
        void addSysLob(void* newRowId, typeObj objId, typeCol col, typeCol intcol, typeObj lObj, typeTs ts);
        void addSysLobCompPart(void* newRowId, typeObj partObj, typeObj lObj);
        void addSysLobFrag(void* newRowId, typeObj fragObj, typeObj parentObj, typeTs ts);
        bool checkSchemaElement(void* rowId) const;
        void deleteSchemaElement(void* rowId);
    };
}

#endif
