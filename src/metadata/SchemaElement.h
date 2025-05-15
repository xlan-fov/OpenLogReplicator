/**
 * 存储模式元素信息的结构体定义
 * 
 * 该文件定义了SchemaElement类，用于存储数据库模式元素的信息，
 * 如表格过滤条件、键定义和标签定义等。
 *
 * @file SchemaElement.h
 * @author Adam Leszczynski (aleszczynski@bersler.com)
 * @copyright Copyright (C) 2018-2025 Adam Leszczynski
 * @license GPL-3.0
 */

#ifndef SCHEMA_ELEMENT_H_
#define SCHEMA_ELEMENT_H_

#include <cctype>
#include <locale>
#include <vector>

#include "../common/DbTable.h"
#include "../common/types/Types.h"

namespace OpenLogReplicator {
    /**
     * 模式元素类 - 存储表和过滤条件的相关信息
     */
    class SchemaElement final {
    public:
        /**
         * 标签类型枚举
         */
        enum class TAG_TYPE : unsigned char {
            NONE,  // 无标签
            ALL,   // 所有列
            PK,    // 主键列
            LIST   // 指定列列表
        };

        std::string condition;       // 过滤条件
        std::string key;             // 键定义(逗号分隔)
        std::string owner;           // 表所有者
        std::string table;           // 表名
        std::string tag;             // 标签定义
        DbTable::OPTIONS options;    // 表选项
        TAG_TYPE tagType{TAG_TYPE::NONE}; // 标签类型
        std::vector<std::string> keyList; // 键列表
        std::vector<std::string> tagList; // 标签列表

        /**
         * 构造函数
         * 
         * @param newOwner 所有者名称
         * @param newTable 表名称
         * @param newOptions 表选项
         */
        SchemaElement(std::string newOwner, std::string newTable, DbTable::OPTIONS newOptions) :
                owner(std::move(newOwner)),
                table(std::move(newTable)),
                options(newOptions) {
        }

        /**
         * 解析键定义字符串
         * 
         * @param value 键定义字符串
         * @param separator 分隔符
         */
        void parseKey(std::string value, const std::string& separator) {
            size_t pos = 0;
            while ((pos = value.find(separator)) != std::string::npos) {
                const std::string val = value.substr(0, pos);
                keyList.push_back(val);
                value.erase(0, pos + separator.length());
            }
            keyList.push_back(value);
        }

        /**
         * 解析标签定义字符串
         * 
         * @param value 标签定义字符串
         * @param separator 分隔符
         */
        void parseTag(std::string value, const std::string& separator) {
            if (value == "[pk]") {
                tagType = TAG_TYPE::PK;
                return;
            }

            if (value == "[all]") {
                tagType = TAG_TYPE::ALL;
                return;
            }

            tagType = TAG_TYPE::LIST;
            size_t pos = 0;
            while ((pos = value.find(separator)) != std::string::npos) {
                const std::string val = value.substr(0, pos);
                tagList.push_back(val);
                value.erase(0, pos + separator.length());
            }
            tagList.push_back(value);
        }
    };
}

#endif
