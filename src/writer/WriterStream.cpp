/* 网络流写入线程实现
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

#include "../builder/Builder.h"
#include "../common/OraProtoBuf.pb.h"
#include "../common/exception/NetworkException.h"
#include "../metadata/Metadata.h"
#include "../stream/Stream.h"
#include "WriterStream.h"

namespace OpenLogReplicator {
    // 构造函数 - 初始化流式写入器
    WriterStream::WriterStream(Ctx* newCtx, std::string newAlias, std::string newDatabase, Builder* newBuilder, Metadata* newMetadata, Stream* newStream) :
            Writer(newCtx, std::move(newAlias), std::move(newDatabase), newBuilder, newMetadata),
            stream(newStream) {
        metadata->bootFailsafe = true;
        ctx->parserThread = this;
    }

    // 析构函数 - 清理资源
    WriterStream::~WriterStream() {
        if (stream != nullptr) {
            delete stream;
            stream = nullptr;
        }
    }

    // 初始化方法 - 初始化写入器并启动服务器
    void WriterStream::initialize() {
        Writer::initialize();
        stream->initializeServer();
    }

    // 获取写入器类型
    std::string WriterStream::getType() const {
        return stream->getName();
    }

    // 处理INFO请求 - 返回数据库状态信息
    void WriterStream::processInfo() {
        response.Clear();
        if (request.database_name() != database) {
            ctx->warning(60035, "unknown database requested, got: " + request.database_name() + ", expected: " + database);
            response.set_code(pb::ResponseCode::INVALID_DATABASE);
            return;
        }

        if (metadata->status == Metadata::STATUS::READY) {
            ctx->logTrace(Ctx::TRACE::WRITER, "info, ready");
            response.set_code(pb::ResponseCode::READY);
            return;
        }

        if (metadata->status == Metadata::STATUS::START) {
            ctx->logTrace(Ctx::TRACE::WRITER, "info, start");
            response.set_code(pb::ResponseCode::STARTING);
        }

        ctx->logTrace(Ctx::TRACE::WRITER, "info, first scn: " + metadata->firstDataScn.toString());
        response.set_code(pb::ResponseCode::REPLICATE);
        response.set_scn(metadata->firstDataScn.getData());
        response.set_c_scn(confirmedScn.getData());
        response.set_c_idx(confirmedIdx);
    }

    // 处理START请求 - 开始数据复制
    void WriterStream::processStart() {
        response.Clear();
        if (request.database_name() != database) {
            ctx->warning(60035, "unknown database requested, got: " + request.database_name() + ", expected: " + database);
            response.set_code(pb::ResponseCode::INVALID_DATABASE);
            return;
        }

        if (metadata->status == Metadata::STATUS::REPLICATE) {
            ctx->logTrace(Ctx::TRACE::WRITER, "client requested start when already started");
            response.set_code(pb::ResponseCode::ALREADY_STARTED);
            response.set_scn(metadata->firstDataScn.getData());
            response.set_c_scn(confirmedScn.getData());
            response.set_c_idx(confirmedIdx);
            return;
        }

        if (metadata->status == Metadata::STATUS::START) {
            ctx->logTrace(Ctx::TRACE::WRITER, "client requested start when already starting");
            response.set_code(pb::ResponseCode::STARTING);
            return;
        }

        // 处理序列号参数
        std::string paramSeq;
        if (request.has_seq()) {
            metadata->startSequence = request.seq();
            paramSeq = ", seq: " + std::to_string(request.seq());
        } else
            metadata->startSequence = Seq::none();

        // 初始化开始位置相关参数
        metadata->startScn = Scn::none();
        metadata->startTime = "";
        metadata->startTimeRel = 0;

        // 根据请求类型设置起始点
        switch (request.tm_val_case()) {
            case pb::RedoRequest::TmValCase::kScn:
                metadata->startScn = request.scn();
                if (metadata->startScn == Scn::none())
                    ctx->info(0, "client requested to start from NOW" + paramSeq);
                else
                    ctx->info(0, "client requested to start from scn: " + metadata->startScn.toString() + paramSeq);
                break;

            case pb::RedoRequest::TmValCase::kTms:
                metadata->startTime = request.tms();
                ctx->info(0, "client requested to start from time: " + metadata->startTime + paramSeq);
                break;

            case pb::RedoRequest::TmValCase::kTmRel:
                metadata->startTimeRel = request.tm_rel();
                ctx->info(0, "client requested to start from relative time: " + std::to_string(metadata->startTimeRel) + paramSeq);
                break;

            default:
                ctx->logTrace(Ctx::TRACE::WRITER, "client requested an invalid starting point");
                response.set_code(pb::ResponseCode::INVALID_COMMAND);
                return;
        }
        
        // 设置开始状态并等待复制器准备就绪
        metadata->setStatusStart(this);
        contextSet(CONTEXT::SLEEP);
        metadata->waitForReplicator(this);

        // 如果已进入复制状态，开始向客户端流式传输数据
        if (metadata->status == Metadata::STATUS::REPLICATE) {
            response.set_code(pb::ResponseCode::REPLICATE);
            response.set_scn(metadata->firstDataScn.getData());
            response.set_c_scn(confirmedScn.getData());
            response.set_c_idx(confirmedIdx);

            ctx->info(0, "streaming to client");
            streaming = true;
        } else {
            ctx->logTrace(Ctx::TRACE::WRITER, "starting failed");
            response.set_code(pb::ResponseCode::FAILED_START);
        }
    }

    // 处理CONTINUE请求 - 继续之前的复制
    void WriterStream::processContinue() {
        response.Clear();
        if (request.database_name() != database) {
            ctx->warning(60035, "unknown database requested, got: " + std::string(request.database_name()) + " instead of " + database);
            response.set_code(pb::ResponseCode::INVALID_DATABASE);
            return;
        }

        // 设置默认值
        metadata->clientScn = confirmedScn;
        metadata->clientIdx = confirmedIdx;
        std::string paramIdx;

        // 0表示继续使用上次值
        if (request.has_c_scn() && request.c_scn() != 0) {
            metadata->clientScn = request.c_scn();

            if (request.has_c_idx())
                metadata->clientIdx = request.c_idx();
            paramIdx = ", idx: " + std::to_string(metadata->clientIdx);
        }
        ctx->info(0, "client requested scn: " + metadata->clientScn.toString() + paramIdx);

        // 重置消息队列并开始流式传输
        resetMessageQueue();
        response.set_code(pb::ResponseCode::REPLICATE);
        ctx->info(0, "streaming to client");
        streaming = true;
    }

    // 处理CONFIRM请求 - 确认客户端已接收消息
    void WriterStream::processConfirm() {
        if (request.database_name() != database) {
            ctx->warning(60035, "unknown database confirmed, got: " + request.database_name() + ", expected: " + database);
            return;
        }

        // 确认所有消息直到指定的SCN和IDX
        while (currentQueueSize > 0 && (queue[0]->lwnScn < Scn(request.c_scn()) ||
                (queue[0]->lwnScn == Scn(request.c_scn()) && queue[0]->lwnIdx <= request.c_idx())))
            confirmMessage(queue[0]);
    }

    // 轮询消息队列 - 检查并处理客户端请求
    void WriterStream::pollQueue() {
        // 客户端未连接时不处理
        if (!stream->isConnected())
            return;

        uint8_t msgR[Stream::READ_NETWORK_BUFFER];
        std::string msgS;

        // 非阻塞方式接收客户端消息
        const uint64_t size = stream->receiveMessageNB(msgR, Stream::READ_NETWORK_BUFFER);

        if (size > 0) {
            request.Clear();
            if (request.ParseFromArray(msgR, static_cast<int>(size))) {
                // 根据当前状态和请求类型处理
                if (streaming) {
                    switch (request.code()) {
                        case pb::RequestCode::INFO:
                            processInfo();
                            response.SerializeToString(&msgS);
                            stream->sendMessage(msgS.c_str(), msgS.length());
                            streaming = false;
                            break;

                        case pb::RequestCode::CONFIRM:
                            processConfirm();
                            break;

                        default:
                            ctx->warning(60032, "unknown request code: " + std::to_string(request.code()));
                            response.Clear();
                            response.set_code(pb::ResponseCode::INVALID_COMMAND);
                            response.SerializeToString(&msgS);
                            stream->sendMessage(msgS.c_str(), msgS.length());
                            break;
                    }
                } else {
                    switch (request.code()) {
                        case pb::RequestCode::INFO:
                            processInfo();
                            response.SerializeToString(&msgS);
                            stream->sendMessage(msgS.c_str(), msgS.length());
                            break;

                        case pb::RequestCode::START:
                            processStart();
                            response.SerializeToString(&msgS);
                            stream->sendMessage(msgS.c_str(), msgS.length());
                            break;

                        case pb::RequestCode::CONTINUE:
                            processContinue();
                            response.SerializeToString(&msgS);
                            stream->sendMessage(msgS.c_str(), msgS.length());
                            break;

                        default:
                            ctx->warning(60032, "unknown request code: " + std::to_string(request.code()));
                            response.Clear();
                            response.set_code(pb::ResponseCode::INVALID_COMMAND);
                            response.SerializeToString(&msgS);
                            stream->sendMessage(msgS.c_str(), msgS.length());
                            break;
                    }
                }
            } else {
                // 解析请求失败时记录二进制内容
                std::ostringstream ss;
                ss << "request decoder[" << std::dec << size << "]: ";
                for (uint64_t i = 0; i < size; ++i)
                    ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<uint>(msgR[i]) << " ";
                ctx->warning(60033, ss.str());
            }
        } else if (errno != EAGAIN)
            throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno));
    }

    // 发送消息到客户端
    void WriterStream::sendMessage(BuilderMsg* msg) {
        // 清除以前的消息内容，设置新消息头
        response.Clear();
        response.set_code(pb::ResponseCode::DATA);
        response.set_scn(msg->scn.getData());
        response.set_c_scn(msg->lwnScn.getData());
        response.set_c_idx(msg->lwnIdx);

        // 尝试解析消息并发送
        bool success = true;
        if (!response.ParseFromArray(msg->data, msg->size)) {
            response.Clear();
            ctx->warning(60034, "invalid protobuf message");
            success = false;
        }

        std::string message;
        if (success && !response.SerializeToString(&message)) {
            response.Clear();
            ctx->warning(60034, "serialization error");
            success = false;
        }

        if (success) {
            try {
                stream->sendMessage(message.c_str(), message.length());
            } catch (NetworkException& ex) {
                ctx->error(ex.code, ex.msg);
                streaming = false;
                throw;
            }
        } else {
            // 发送错误消息
            response.set_code(pb::ResponseCode::ERROR);
            response.SerializeToString(&message);
            stream->sendMessage(message.c_str(), message.length());
        }

        // 确认消息已处理
        confirmMessage(msg);
    }
}
