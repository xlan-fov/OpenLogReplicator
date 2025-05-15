/* 运行时异常类定义
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

#ifndef RUNTIME_EXCEPTION_H_
#define RUNTIME_EXCEPTION_H_

#include <exception>
#include <string>

namespace OpenLogReplicator {
    // 运行时异常类 - 处理程序运行时产生的异常
    class RuntimeException : public std::exception {
    public:
        const uint64_t code;     // 错误代码
        const std::string msg;   // 错误消息

        // 构造函数
        RuntimeException(uint64_t newCode, std::string newMsg) :
                code(newCode),
                msg(std::move(newMsg)) {
        }

        // 获取异常消息
        [[nodiscard]] const char* what() const noexcept override {
            return msg.c_str();
        }
    };
}

#endif
