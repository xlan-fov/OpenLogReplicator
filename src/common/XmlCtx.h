/* XML上下文管理类头文件
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

#ifndef XML_CTX_H_
#define XML_CTX_H_

#include <map>
#include <unordered_map>

#include "../common/Ctx.h"
#include "../common/table/TablePack.h"
#include "../common/table/XdbXNm.h"
#include "../common/table/XdbXQn.h"
#include "../common/table/XdbXPt.h"
#include "../common/types/RowId.h"
#include "../common/types/Types.h"

namespace OpenLogReplicator {
    // 前向声明
    class Ctx;

    // XML上下文类 - 负责处理Oracle XML类型数据
    class XmlCtx final {
    protected:
        Ctx* ctx;          // 上下文对象
        bool experimental; // 是否处于实验模式

    public:
        // XML命名空间常量
        static constexpr std::string_view NS_XDBC{"http://xmlns.oracle.com/xdb/xdbconfig.xsd"};
        static constexpr std::string_view NS_XDBCA{"http://xmlns.oracle.com/xdb/access.xsd"};
        static constexpr std::string_view NS_XSD{"http://www.w3.org/2001/XMLSchema"};
        static constexpr std::string_view NS_XSI{"http://www.w3.org/2001/XMLSchema-instance"};
        static constexpr std::string_view NS_PLSQL{"http://xmlns.oracle.com/plsql/vocabulary"};
        static constexpr std::string_view NS_XML{"http://www.w3.org/XML/1998/namespace"};
        static constexpr std::string_view NS_XMLNS{"http://www.w3.org/2000/xmlns/"};
        static constexpr std::string_view NS_XMLSOAP{"http://schemas.xmlsoap.org/wsdl/soap/"};
        static constexpr std::string_view NS_WSDL{"http://schemas.xmlsoap.org/wsdl/"};
        static constexpr std::string_view NS_XDB{"http://xmlns.oracle.com/xdb"};
        static constexpr std::string_view NS_XFILES{"http://xmlns.oracle.com/xdb/xfiles"};
        static constexpr std::string_view NS_XSC{"http://www.w3.org/2001/XMLSchema"};
        static constexpr std::string_view NS_XDBRESOURCE{"http://xmlns.oracle.com/xdb/XDBResource.xsd"};
        static constexpr std::string_view NS_XDIFF{"http://xmlns.oracle.com/xdb/xdiff.xsd"};
        static constexpr std::string_view NS_RESOURCE{"http://xmlns.oracle.com/xdb/XDBResource.xsd"};
        static constexpr std::string_view NS_XSL{"http://www.w3.org/1999/XSL/Transform"};
        static constexpr std::string_view NS_DAV{"DAV:"};
        static constexpr std::string_view NS_SOAP_ENVELOPE{"http://schemas.xmlsoap.org/soap/envelope/"};
        static constexpr std::string_view NS_SOAP_ENC{"http://schemas.xmlsoap.org/soap/encoding/"};
        static constexpr std::string_view NS_ORACLE_PLSQLSESSION{"http://xmlns.oracle.com/orawsv/ORASSO/PLSQLSESSIONINFO"};
        static constexpr std::string_view NS_URI{"http://purl.org/dc/elements/1.1/"};
        static constexpr std::string_view NS_SEC{"http://xmlns.oracle.com/xdb/security.xsd"};
        static constexpr std::string_view NS_XDBSCH{"http://xmlns.oracle.com/xdb/XDBSchema.xsd"};

        // XML类型常量
        static constexpr uint64_t XMLTYPE_SCHEMALESS{0x00000000}; // 无架构XML
        static constexpr uint64_t XMLTYPE_XML_DATA{0x00000001};   // XML数据
        static constexpr uint64_t XMLTYPE_CSV_DATA{0x00000002};   // CSV数据
        static constexpr uint64_t XMLTYPE_SKIP{0x00000003};       // 跳过
        static constexpr uint64_t XMLTYPE_OTHER{0xFFFFFFFF};      // 其他类型

        std::unordered_map<std::string, std::string> xmlNamespaces; // XML命名空间映射
        
        // 构造函数
        explicit XmlCtx(Ctx* newCtx);
        
        // 添加命名空间
        void addNamespace(const std::string& nspace, const std::string& path);
        
        // XML类型解析方法
        [[nodiscard]] static uint64_t getXmlTypeDataFormat(const uint8_t* data, uint64_t size);
        
        // XML解析方法
        [[nodiscard]] std::string parseXmlData(const uint8_t* data, uint64_t size) const;
        [[nodiscard]] std::string parseXmlDataRaw(const uint8_t* data, uint64_t size) const;
        [[nodiscard]] std::string convertXmlNamespace(const std::string& ns) const;
    };
}
#endif
