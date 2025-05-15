/* 数据异常类定义
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

#ifndef DATA_EXCEPTION_H_
#define DATA_EXCEPTION_H_

#include <exception>
#include <string>

namespace OpenLogReplicator {
    // 数据异常类 - 处理与数据内容或格式相关的异常
    class DataException : public std::exception {
    public:
        const uint64_t code;     // 错误代码
        const std::string msg;   // 错误消息

        // 构造函数
        DataException(uint64_t newCode, std::string newMsg) :
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
