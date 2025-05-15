/**
 * 序列化器类头文件
 * 
 * 该文件定义了Serializer基类，提供了元数据序列化和反序列化的接口。
 * Serializer类被用于将数据库元数据结构持久化为文件存储格式（如JSON），
 * 以及从存储格式恢复元数据结构。
 *
 * @file Serializer.h
 * @author Adam Leszczynski (aleszczynski@bersler.com)
 * @copyright Copyright (C) 2018-2025 Adam Leszczynski
 * @license GPL-3.0
 */

#ifndef SERIALIZER_H_
#define SERIALIZER_H_

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <unordered_map>
#include <vector>

#include "../common/types/Types.h"

namespace OpenLogReplicator {
    class Ctx;
    class Metadata;
    class XmlCtx;

    /**
     * 序列化器基类 - 提供元数据序列化和反序列化的接口
     */
    class Serializer {
    public:
        Serializer() = default;
        virtual ~Serializer() = default;
        Serializer(const Serializer&) = delete;
        Serializer& operator=(const Serializer&) = delete;

        /**
         * 将数据从字符串反序列化到Metadata对象
         * 
         * @param metadata 目标元数据对象
         * @param ss 包含序列化数据的字符串
         * @param fileName 文件名(用于错误报告)
         * @param msgs 错误/警告消息输出
         * @param tablesUpdated 更新的表映射
         * @param loadMetadata 是否加载元数据
         * @param loadSchema 是否加载schema
         * @return 反序列化是否成功
         */
        [[nodiscard]] virtual bool deserialize(Metadata* metadata, const std::string& ss, const std::string& fileName, std::vector<std::string>& msgs,
                                              std::unordered_map<typeObj, std::string>& tablesUpdated, bool loadMetadata, bool loadSchema) = 0;

        /**
         * 将数据从Metadata对象序列化到输出流
         * 
         * @param metadata 源元数据对象
         * @param ss 输出流
         * @param storeSchema 是否存储完整schema
         */
        virtual void serialize(Metadata* metadata, std::ostringstream& ss, bool storeSchema) = 0;
    };
}

#endif
