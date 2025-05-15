#ifndef REDO_LOG_RECORD_H_
#define REDO_LOG_RECORD_H_

#include <cstring>
#include <deque>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../common/Ctx.h"
#include "../common/types/FileOffset.h"
#include "../common/types/OpCode.h"
#include "../common/types/Scn.h"
#include "../common/types/Seq.h"
#include "../common/types/Types.h"
#include "../common/types/Xid.h"

namespace OpenLogReplicator {
    // 前向声明
    class Transaction;
    class TransactionChunk;

    // 重做日志记录类 - 表示Oracle重做日志文件中的单个记录
    class RedoLogRecord {
    public:
        // 重做日志版本常量
        static constexpr uint32_t REDO_VERSION_10_1 = 0x0A01;  // Oracle 10g R1
        static constexpr uint32_t REDO_VERSION_10_2 = 0x0A20;  // Oracle 10g R2
        static constexpr uint32_t REDO_VERSION_11_1 = 0x0B10;  // Oracle 11g R1
        static constexpr uint32_t REDO_VERSION_11_2 = 0x0B20;  // Oracle 11g R2
        static constexpr uint32_t REDO_VERSION_12_1 = 0x0C10;  // Oracle 12c R1
        static constexpr uint32_t REDO_VERSION_12_2 = 0x0C20;  // Oracle 12c R2
        static constexpr uint32_t REDO_VERSION_18_0 = 0x1200;  // Oracle 18c
        static constexpr uint32_t REDO_VERSION_19_0 = 0x1300;  // Oracle 19c
        static constexpr uint32_t REDO_VERSION_21_0 = 0x1500;  // Oracle 21c

        // 标志位常量
        static constexpr uint64_t FLAG_ROLL_BACK = 0x01;         // 回滚标志
        static constexpr uint64_t FLAG_INCOMPLETE = 0x04;        // 不完整记录标志
        static constexpr uint64_t FLAG_FIRST_IN_TRANSACTION = 0x08; // 事务中的第一条记录
        static constexpr uint64_t FLAG_CHUNK = 0x10;            // 分块标志
        static constexpr uint64_t FLAG_DISABLED = 0x20;         // 禁用标志
        static constexpr uint64_t FLAG_ROLLBACK_STATEMENT = 0x40; // 回滚语句标志
        static constexpr uint64_t FLAG_LOBEMPTY = 0x80;          // LOB空标志
        static constexpr uint64_t FLAG_TEMPORARY = 0x100;        // 临时标志
        static constexpr uint64_t FLAG_VARWIDTH_SCHEMA = 0x200;  // 变宽模式标志
        static constexpr uint64_t FLAG_UNSUPPORTED_REDO = 0x400; // 不支持的重做标志
        static constexpr uint64_t FLAG_KTUCF = 0x800;           // KTUCF标志
        static constexpr uint64_t FLAG_LOBMISS = 0x1000;        // LOB缺失标志
        static constexpr uint64_t FLAG_ADAPTIVE = 0x2000;       // 自适应标志
        static constexpr uint64_t FLAG_DIRECT = 0x4000;         // 直接标志
        static constexpr uint64_t FLAG_TRANSACTION_FREE = 0x8000; // 事务空闲标志
        static constexpr uint64_t FLAG_PROCESSED = 0x10000;     // 已处理标志
        static constexpr uint64_t FLAG_ALT_LMN = 0x20000;       // ALT LMN标志
        static constexpr uint64_t FLAG_COMMIT_ORDER = 0x40000;  // 提交顺序标志
        static constexpr uint64_t FLAG_KDO_KTEOP = 0x80000;     // KDO KTEOP标志
        static constexpr uint64_t FLAG_SUPPRESS = 0x100000;     // 抑制标志
        static constexpr uint64_t FLAG_BIGDATA = 0x200000;      // 大数据标志
        static constexpr uint64_t FLAG_BIG_KTUBL = 0x400000;    // 大KTUBL标志
        static constexpr uint64_t FLAG_KTUCF_CHECK = 0x800000;  // KTUCF检查标志
        static constexpr uint64_t FLAG_PKT_INT = 0x1000000;     // PKT INT标志
        static constexpr uint64_t FLAG_OBJN = 0x2000000;        // OBJN标志
        static constexpr uint64_t FLAG_DEPENDENT = 0x4000000;   // 依赖标志
        static constexpr uint64_t FLAG_XAROLLBACK = 0x8000000;  // XA回滚标志
        static constexpr uint64_t FLAG_SAME_SLOT = 0x10000000;  // 相同槽标志
        static constexpr uint64_t FLAG_SUPP_LOG_BDBA = 0x20000000; // 补充日志BDBA标志
        static constexpr uint64_t FLAG_PIECE = 0x40000000;      // 片段标志
        static constexpr uint64_t FLAG_KDO_NOREDO_OP_SEQ = 0x80000000; // KDO NOREDO OP SEQ标志

        // LMN标志位
        static constexpr uint64_t LMN_REDO = 0x01;        // LMN重做标志
        static constexpr uint64_t LMN_LOB_ORIG = 0x02;    // LMN LOB原始标志
        static constexpr uint64_t LMN_PART = 0x04;        // LMN分区标志
        static constexpr uint64_t LMN_XTYPE = 0x08;       // LMN XTYPE标志
        static constexpr uint64_t LMN_COL_PROPERTY = 0x10; // LMN列属性标志
        static constexpr uint64_t LMN_ALTER2 = 0x80;      // LMN ALTER2标志

        // OP标志位常量
        static constexpr uint64_t OP_ROWDEPENDENCIES = 0x00000001; // 行依赖标志

        // FB标志位常量
        static constexpr uint8_t FB_P = 0x01;             // FB P标志
        static constexpr uint8_t FB_N = 0x02;             // FB N标志
        static constexpr uint8_t FB_F = 0x04;             // FB F标志
        static constexpr uint8_t FB_PFK = 0x08;           // FB PFK标志
        static constexpr uint8_t FB_IPK = 0x10;           // FB IPK标志
        static constexpr uint8_t FB_D = 0x20;             // FB D标志
        static constexpr uint8_t FB_K = 0x40;             // FB K标志
        static constexpr uint8_t FB_L = 0x80;             // FB L标志

        // 记录位置和结构信息
        typeBlk blockNumber;             // 块号
        typeBlock block;                 // 块
        typeSubBlock subBlock;           // 子块
        typeScn scn;                     // SCN号
        typeScn checkpoint;              // 检查点SCN
        typeField fieldCnt;              // 字段计数
        typeField fieldSizes[256];       // 字段大小数组
        typeRecord recordNumber;         // 记录号
        typeField fieldLengths[256];     // 字段长度数组
        uint32_t version;                // 版本
        typeCC cc;                       // 列计数
        typeCC nRow;                     // 行数
        typeSize dataOffset;             // 数据偏移
        typeSize dataLength;             // 数据长度
        uint64_t flags;                  // 标志位
        Xid xid;                         // 事务ID
        uint8_t* data_;                  // 数据指针
        OpCode opCode;                   // 操作码
        uint8_t fb;                      // FB标志
        uint64_t op;                     // 操作
        typeObj obj;                     // 对象
        typeDataObj dataObj;             // 数据对象
        typeDba bdba;                    // 数据块地址
        typeSlot slot;                   // 槽位
        typeDba suppLogBdba;             // 补充日志数据块地址
        typeSlot suppLogSlot;            // 补充日志槽位
        
        // 日志记录偏移量信息
        typePos nullsDelta;              // NULL值偏移
        typePos colNumsDelta;            // 列号偏移
        typePos suppLogNumsDelta;        // 补充日志号偏移
        typePos suppLogLenDelta;         // 补充日志长度偏移
        typePos rowData;                 // 行数据
        typePos suppLogRowData;          // 补充日志行数据
        typePos sizeDelt;                // 大小偏移
        typePos slotsDelta;              // 槽位偏移
        typePos rowSizesDelta;           // 行大小偏移
        typeObj bdba2;                   // 数据块地址2
        typeObj bdba3;                   // 数据块地址3
        typeSlot slot2;                  // 槽位2
        typeCol suppLogBefore;           // 补充日志前
        typeCol suppLogAfter;            // 补充日志后
        typeCC suppLogCC;                // 补充日志列计数
        uint8_t suppLogFb;               // 补充日志FB
        bool compressed;                 // 是否压缩
        
        // 定位信息
        typeBlk lastBlock;               // 最后块
        FileOffset fileOffset;           // 文件偏移
        
        // 所属事务和事务块
        Transaction* transaction;         // 事务指针
        TransactionChunk* transactionChunk; // 事务块指针

        // 构造和销毁
        RedoLogRecord();                 // 构造函数
        ~RedoLogRecord();                // 析构函数

        // 获取数据指针
        const uint8_t* data(uint64_t offset = 0) const;
        
        // 字段操作方法
        static void skipEmptyFields(Ctx* ctx, const RedoLogRecord* redoLogRecord, typeField& fieldNum, typePos& fieldPos, typeSize& fieldSize);
        static void nextField(Ctx* ctx, const RedoLogRecord* redoLogRecord, typeField& fieldNum, typePos& fieldPos, typeSize& fieldSize, uint32_t _from);
        static bool nextFieldOpt(Ctx* ctx, const RedoLogRecord* redoLogRecord, typeField& fieldNum, typePos& fieldPos, typeSize& fieldSize, uint32_t _from);
    };
}

#endif
