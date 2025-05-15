# 构建配置文件
# Copyright (C) 2018-2025 Adam Leszczynski (aleszczynski@bersler.com)
#
# This file is part of OpenLogReplicator.
#
# OpenLogReplicator is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as published
# by the Free Software Foundation; either version 3, or (at your option)
# any later version.
#
# OpenLogReplicator is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
# Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with OpenLogReplicator; see the file LICENSE;  If not see
# <http://www.gnu.org/licenses/>.

# 通用模块源文件列表
list(APPEND ListCommon
        common/ClockHW.cpp
        common/Ctx.cpp
        common/DbLob.cpp
        common/DbTable.cpp
        common/LobCtx.cpp
        common/LobData.cpp
        common/LobKey.cpp
        common/MemoryManager.cpp
        common/Thread.cpp
        common/XmlCtx.cpp
        common/exception/BootException.cpp
        common/exception/ConfigurationException.cpp
        common/exception/DataException.cpp
        common/exception/NetworkException.cpp
        common/exception/RedoLogException.cpp
        common/exception/RuntimeException.cpp
        common/expression/BoolValue.cpp
        common/expression/Expression.cpp
        common/expression/StringValue.cpp
        common/expression/Token.cpp
        common/metrics/Metrics.cpp
        common/types/Data.cpp)

# 复制器模块源文件列表
list(APPEND ListReplicator
        replicator/Replicator.cpp
        replicator/ReplicatorBatch.cpp)

# 本地化模块源文件列表
list(APPEND ListLocales
        locales/CharacterSet.cpp
        locales/CharacterSet16bit.cpp
        locales/CharacterSet7bit.cpp
        locales/CharacterSet8bit.cpp
        locales/CharacterSetAL16UTF16.cpp
        locales/CharacterSetAL32UTF8.cpp
        locales/CharacterSetJA16EUC.cpp
        locales/CharacterSetJA16EUCTILDE.cpp
        locales/CharacterSetJA16SJIS.cpp
        locales/CharacterSetJA16SJISTILDE.cpp
        locales/CharacterSetKO16KSCCS.cpp
        locales/CharacterSetUTF8.cpp
        locales/CharacterSetZHS16GBK.cpp
        locales/CharacterSetZHS32GB18030.cpp
        locales/CharacterSetZHT16HKSCS31.cpp
        locales/CharacterSetZHT32EUC.cpp
        locales/CharacterSetZHT32TRIS.cpp
        locales/Locales.cpp)

# 构建器模块源文件列表
list(APPEND ListBuilder
        builder/Builder.cpp
        builder/BuilderJson.cpp
        builder/SystemTransaction.cpp)

# 解析器模块源文件列表
list(APPEND ListParser
        parser/Parser.cpp
        parser/Transaction.cpp
        parser/TransactionBuffer.cpp)

# 读取器模块源文件列表
list(APPEND ListReader
        reader/Reader.cpp
        reader/ReaderFilesystem.cpp)

# 元数据模块源文件列表
list(APPEND ListMetadata
        metadata/Checkpoint.cpp
        metadata/Metadata.cpp
        metadata/Schema.cpp
        metadata/Serializer.cpp
        metadata/SerializerJson.cpp)

# 状态模块源文件列表
list(APPEND ListState
        state/State.cpp
        state/StateDisk.cpp)

# 写入器模块源文件列表
list(APPEND ListWriter
        writer/Writer.cpp
        writer/WriterDiscard.cpp
        writer/WriterFile.cpp)

# 条件编译：Oracle接口支持
if (WITH_OCI)
    list(APPEND ListReplicator
            replicator/DatabaseConnection.cpp
            replicator/DatabaseEnvironment.cpp
            replicator/DatabaseStatement.cpp
            replicator/ReplicatorOnline.cpp)
endif ()

# 条件编译：Kafka支持
if (WITH_RDKAFKA)
    list(APPEND ListWriter
            writer/WriterKafka.cpp)
endif ()

# 条件编译：Prometheus支持
if (WITH_PROMETHEUS)
    list(APPEND ListCommon
            common/metrics/MetricsPrometheus.cpp)
endif ()

# 条件编译：Protocol Buffers支持
if (WITH_PROTOBUF)
    list(APPEND ListCommon
            common/OraProtoBuf.pb.cpp)
    list(APPEND ListBuilder
            builder/BuilderProtobuf.cpp)
    list(APPEND ListStream
            stream/Stream.cpp
            stream/StreamNetwork.cpp)
    list(APPEND ListWriter
            writer/WriterStream.cpp)

    # 条件编译：ZeroMQ支持
    if (WITH_ZEROMQ)
        list(APPEND ListStream
                stream/StreamZeroMQ.cpp)
    endif ()
endif ()

# 构建各模块的目标库
add_library(LibCommon OBJECT ${ListCommon})
add_library(LibReplicator OBJECT ${ListReplicator})
add_library(LibLocales OBJECT ${ListLocales})
add_library(LibBuilder OBJECT ${ListBuilder})
add_library(LibParser OBJECT ${ListParser})
add_library(LibReader OBJECT ${ListReader})
add_library(LibMetadata OBJECT ${ListMetadata})
add_library(LibState OBJECT ${ListState})
add_library(LibWriter OBJECT ${ListWriter})

# 主程序源文件和链接库
target_sources(OpenLogReplicator PUBLIC OpenLogReplicator.cpp main.cpp)
target_link_libraries(OpenLogReplicator LibCommon)
target_link_libraries(OpenLogReplicator LibReplicator)
target_link_libraries(OpenLogReplicator LibLocales)
target_link_libraries(OpenLogReplicator LibBuilder)
target_link_libraries(OpenLogReplicator LibParser)
target_link_libraries(OpenLogReplicator LibReader)
target_link_libraries(OpenLogReplicator LibMetadata)
target_link_libraries(OpenLogReplicator LibState)
target_link_libraries(OpenLogReplicator LibWriter)

# 条件编译：Stream客户端
if (WITH_PROTOBUF)
    add_library(LibStream ${ListStream})
    target_link_libraries(OpenLogReplicator LibStream)

    # StreamClient应用程序
    target_sources(StreamClient PUBLIC StreamClient.cpp)
    target_link_libraries(StreamClient LibCommon)
    target_link_libraries(StreamClient LibStream)
endif ()
