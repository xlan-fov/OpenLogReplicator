/**
 * 序列号类型定义
 * 
 * 该文件定义了Seq类，表示Oracle重做日志的序列号。
 * 序列号用于标识重做日志文件的顺序，是复制过程中的关键标识符。
 *
 * @file Seq.h
 * @author Adam Leszczynski (aleszczynski@bersler.com)
 * @copyright Copyright (C) 2018-2025 Adam Leszczynski
 * @license GPL-3.0
 */

#ifndef SEQ_H_
#define SEQ_H_

#include <iomanip>
#include <ostream>

namespace OpenLogReplicator {
    /**
     * 序列号类 - 表示Oracle重做日志的序列号
     */
    class Seq final {
        uint32_t data{0};                  // 序列号数据
        static constexpr uint32_t NONE{0xFFFFFFFF}; // 表示无效序列号的常量

    public:
        /** 默认构造函数 */
        Seq() = default;

        /**
         * 获取表示"无"的序列号值
         * 
         * @return 无效序列号
         */
        static Seq none() {
            return Seq(NONE);
        }

        /**
         * 获取零序列号值
         * 
         * @return 零序列号
         */
        static Seq zero() {
            return Seq{};
        }

        /**
         * 从32位整数构造序列号
         * 
         * @param newData 序列号值
         */
        explicit Seq(uint32_t newData) : data(newData) {
        }

        /** 析构函数 */
        ~Seq() = default;

        /**
         * 将序列号转换为字符串
         * 
         * @return 序列号的字符串表示
         */
        [[nodiscard]] std::string toString() const {
            return std::to_string(data);
        }

        /**
         * 将序列号转换为十六进制字符串
         * 
         * @param width 字符串宽度
         * @return 序列号的十六进制字符串表示
         */
        [[nodiscard]] std::string toStringHex(int width) const {
            std::stringstream ss;
            ss << "0x" << std::setfill('0') << std::setw(width) << std::hex << data;
            return ss.str();
        }

        /**
         * 获取序列号数据
         * 
         * @return 序列号数据
         */
        [[nodiscard]] uint32_t getData() const {
            return this->data;
        }

        /**
         * 前缀递增运算符
         * 
         * @return 递增后的序列号
         */
        Seq operator++() {
            ++data;
            return *this;
        }

        /**
         * 相等比较运算符
         * 
         * @param other 另一个序列号
         * @return 如果相等则返回true，否则返回false
         */
        bool operator==(const Seq other) const {
            return data == other.data;
        }

        /**
         * 不等比较运算符
         * 
         * @param other 另一个序列号
         * @return 如果不等则返回true，否则返回false
         */
        bool operator!=(const Seq other) const {
            return data != other.data;
        }

        /**
         * 小于比较运算符
         * 
         * @param other 另一个序列号
         * @return 如果小于则返回true，否则返回false
         */
        bool operator<(const Seq other) const {
            return data < other.data;
        }

        /**
         * 小于等于比较运算符
         * 
         * @param other 另一个序列号
         * @return 如果小于等于则返回true，否则返回false
         */
        bool operator<=(const Seq other) const {
            return data <= other.data;
        }

        /**
         * 大于比较运算符
         * 
         * @param other 另一个序列号
         * @return 如果大于则返回true，否则返回false
         */
        bool operator>(const Seq other) const {
            return data > other.data;
        }

        /**
         * 大于等于比较运算符
         * 
         * @param other 另一个序列号
         * @return 如果大于等于则返回true，否则返回false
         */
        bool operator>=(const Seq other) const {
            return data >= other.data;
        }

        /**
         * 赋值运算符
         * 
         * @param newData 新的序列号数据
         * @return 更新后的序列号
         */
        Seq& operator=(uint32_t newData) {
            data = newData;
            return *this;
        }
    };
}

namespace std {
    template<>
    struct hash<OpenLogReplicator::Seq> {
        size_t operator()(const OpenLogReplicator::Seq seq) const {
            return hash<uint32_t>()(seq.getData());
        }
    };
}

#endif
