/**
 * SCN(系统变更号)类型定义
 * 
 * 该文件定义了Scn类，表示Oracle数据库中的系统变更号。
 * SCN是Oracle中唯一标识数据库变更的数字，按严格递增顺序分配。
 *
 * @file Scn.h
 * @author Adam Leszczynski (aleszczynski@bersler.com)
 * @copyright Copyright (C) 2018-2025 Adam Leszczynski
 * @license GPL-3.0
 */

#ifndef SCN_H_
#define SCN_H_

#include <iomanip>
#include <ostream>

namespace OpenLogReplicator {
    /**
     * SCN(系统变更号)类 - 表示Oracle数据库中的系统变更号
     */
    class Scn final {
        uint64_t data{0};                  // SCN数据
        static constexpr uint64_t NONE{0xFFFFFFFFFFFFFFFF}; // 表示无效SCN的常量

    public:
        /** 默认构造函数 */
        Scn() = default;

        /**
         * 获取表示"无"的SCN值
         * 
         * @return 无效SCN
         */
        static Scn none() {
            return Scn(NONE);
        }

        /**
         * 获取零SCN值
         * 
         * @return 零SCN
         */
        static Scn zero() {
            return Scn{};
        }

        /**
         * 从64位整数构造SCN
         * 
         * @param newData SCN值
         */
        explicit Scn(uint64_t newData) : data(newData) {
        }

        /**
         * 从8个字节构造SCN
         */
        explicit Scn(uint8_t newByte0, uint8_t newByte1, uint8_t newByte2, uint8_t newByte3, uint8_t newByte4, uint8_t newByte5, uint8_t newByte6,
                     uint8_t newByte7) :
                data(static_cast<uint64_t>(newByte0) | (static_cast<uint64_t>(newByte1) << 8) |
                        (static_cast<uint64_t>(newByte2) << 16) | (static_cast<uint64_t>(newByte3) << 24) |
                        (static_cast<uint64_t>(newByte4) << 32) | (static_cast<uint64_t>(newByte5) << 40) |
                        (static_cast<uint64_t>(newByte6) << 48) | (static_cast<uint64_t>(newByte7) << 56)) {
        }

        /**
         * 从6个字节构造SCN
         */
        explicit Scn(uint8_t newByte0, uint8_t newByte1, uint8_t newByte2, uint8_t newByte3, uint8_t newByte4, uint8_t newByte5) :
                data(static_cast<uint64_t>(newByte0) | (static_cast<uint64_t>(newByte1) << 8) |
                        (static_cast<uint64_t>(newByte2) << 16) | (static_cast<uint64_t>(newByte3) << 24) |
                        (static_cast<uint64_t>(newByte4) << 32) | (static_cast<uint64_t>(newByte5) << 40)) {
        }

        /**
         * 从两个32位整数构造SCN
         */
        explicit Scn(uint32_t newData0, uint32_t newData1) :
                data((static_cast<uint64_t>(newData0) << 32) | newData1) {
        }

        /** 析构函数 */
        ~Scn() = default;

        /**
         * 转换SCN为48位格式字符串
         * 
         * 将SCN转换为0xXXXX.XXXXXXXX格式的字符串表示，
         * 这是Oracle 12c之前版本常用的SCN表示格式。
         * 
         * @return 格式化的SCN字符串
         */
        [[nodiscard]] std::string to48() const {
            std::stringstream ss;
            ss << "0x" << std::setfill('0') << std::setw(4) << std::hex << (static_cast<uint32_t>(data >> 32) & 0xFFFF) <<
                    "." << std::setw(8) << (data & 0xFFFFFFFF);
            return ss.str();
        }

        /**
         * 转换SCN为64位十六进制格式字符串
         * 
         * 将SCN转换为完整的64位十六进制字符串表示(0xXXXXXXXXXXXXXXXX)。
         * 
         * @return 64位十六进制格式的SCN字符串
         */
        [[nodiscard]] std::string to64() const {
            std::stringstream ss;
            ss << "0x" << std::setfill('0') << std::setw(16) << std::hex << data;
            return ss.str();
        }

        /**
         * 转换SCN为分段64位十六进制格式
         * 
         * 将SCN转换为0xXXXX.XXXX.XXXXXXXX格式的字符串，
         * 这种格式在某些Oracle版本的日志中使用。
         * 
         * @return 分段格式的SCN字符串
         */
        [[nodiscard]] std::string to64D() const {
            std::stringstream ss;
            ss << "0x" << std::setfill('0') << std::setw(4) << std::hex << (static_cast<uint32_t>(data >> 48) & 0xFFFF) <<
                    "." << std::setw(4) << (static_cast<uint32_t>(data >> 32) & 0xFFFF) << "." << std::setw(8) << (data & 0xFFFFFFFF);
            return ss.str();
        }

        [[nodiscard]] std::string toStringHex12() const {
            std::stringstream ss;
            ss << "0x" << std::setfill('0') << std::setw(12) << std::hex << data;
            return ss.str();
        }

        [[nodiscard]] std::string toStringHex16() const {
            std::stringstream ss;
            ss << "0x" << std::setfill('0') << std::setw(16) << std::hex << (data & 0xFFFF7FFFFFFFFFFF);
            return ss.str();
        }

        [[nodiscard]] std::string toString() const {
            return std::to_string(data);
        }

        [[nodiscard]] uint64_t getData() const {
            return this->data;
        }

        /**
         * 比较相等运算符
         */
        bool operator==(const Scn other) const {
            return data == other.data;
        }

        /**
         * 比较不等运算符
         */
        bool operator!=(const Scn other) const {
            return data != other.data;
        }

        /**
         * 小于比较运算符
         */
        bool operator<(const Scn other) const {
            return data < other.data;
        }

        /**
         * 小于等于比较运算符
         */
        bool operator<=(const Scn other) const {
            return data <= other.data;
        }

        /**
         * 大于比较运算符
         */
        bool operator>(const Scn other) const {
            return data > other.data;
        }

        /**
         * 大于等于比较运算符
         */
        bool operator>=(const Scn other) const {
            return data >= other.data;
        }

        Scn& operator=(uint64_t newData) {
            data = newData;
            return *this;
        }
    };
}

namespace std {
    template<>
    struct hash<OpenLogReplicator::Scn> {
        size_t operator()(const OpenLogReplicator::Scn scn) const {
            return hash<uint64_t>()(scn.getData());
        }
    };
}

#endif
