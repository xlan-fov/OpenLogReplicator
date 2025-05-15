/* Kafka写入器类头文件
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

#ifndef WRITER_KAFKA_H_
#define WRITER_KAFKA_H_

#include "Writer.h"
#include <librdkafka/rdkafka.h>

namespace OpenLogReplicator {
    // Kafka写入器类 - 将变更数据直接发送到Kafka消息队列
    class WriterKafka final : public Writer {
    protected:
        std::string topic;                        // Kafka主题名称
        char errStr[512]{};                       // 错误信息缓冲区
        std::map<std::string, std::string> properties;  // Kafka配置属性
        rd_kafka_t* rk{nullptr};                  // Kafka生产者实例
        rd_kafka_topic_t* rkt{nullptr};           // Kafka主题实例
        rd_kafka_conf_t* conf{nullptr};           // Kafka配置

        // 回调函数
        static void dr_msg_cb(rd_kafka_t* rkCb, const rd_kafka_message_t* rkMessage, void* opaque);  // 消息投递回调
        static void error_cb(rd_kafka_t* rkCb, int err, const char* reason, void* opaque);           // 错误回调
        static void logger_cb(const rd_kafka_t* rkCb, int level, const char* fac, const char* buf);  // 日志回调

        void sendMessage(BuilderMsg* msg) override;  // 发送消息到Kafka
        std::string getType() const override;        // 获取写入器类型
        void pollQueue() override;                   // 轮询消息队列

    public:
        static constexpr uint64_t MAX_KAFKA_MESSAGE_MB = 953;  // 最大Kafka消息大小(MB)

        // 构造函数
        WriterKafka(Ctx* newCtx, std::string newAlias, std::string newDatabase, Builder* newBuilder, Metadata* newMetadata,
                   std::string newTopic);
        // 析构函数
        ~WriterKafka() override;

        void addProperty(std::string key, std::string value);  // 添加Kafka配置属性
        void initialize() override;                            // 初始化Kafka生产者
    };
}

#endif
