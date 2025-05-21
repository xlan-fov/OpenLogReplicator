# OpenLogReplicator 部署指南

本文档详细说明如何从源码构建、配置和部署 OpenLogReplicator。

## 1. 系统要求

### 1.1 硬件要求

- CPU: 至少2核心，推荐4核心或更多
- 内存: 至少4GB，推荐8GB或更多（取决于数据库大小和事务量）
- 磁盘: 至少20GB空闲空间，用于源码、构建文件和运行时存储

### 1.2 操作系统

OpenLogReplicator 主要在以下系统上开发和测试：

- Linux (首选 Debian/Ubuntu)
- 其他 Linux 发行版（如 CentOS/RHEL）可能需要少量修改

**注意:** 不支持 Windows 系统。MacOS 未经过全面测试。

## 2. 前置依赖

### 2.1 基础开发工具

```bash
# Debian/Ubuntu
sudo apt-get update
sudo apt-get install -y build-essential cmake git pkg-config libssl-dev libz-dev

# CentOS/RHEL
sudo yum install -y gcc gcc-c++ make cmake git openssl-devel zlib-devel
```

### 2.2 编译器要求

- GCC 4.9+ 或 Clang 3.4+（推荐使用 GCC 7+ 或 Clang 5+）
- 必须支持 C++11 标准

### 2.3 可选依赖（根据需要安装）

#### Oracle OCI 客户端（用于在线模式）

```bash
# 下载 Oracle Instant Client，选择与目标数据库兼容的版本
# 从 https://www.oracle.com/database/technologies/instant-client/downloads.html

# 安装 Oracle Instant Client 后设置环境变量
export ORACLE_HOME=/path/to/instantclient
export LD_LIBRARY_PATH=$ORACLE_HOME:$LD_LIBRARY_PATH
```

#### Kafka 支持

```bash
# Debian/Ubuntu
sudo apt-get install -y librdkafka-dev

# CentOS/RHEL
sudo yum install -y librdkafka-devel
```

#### ZeroMQ 支持

```bash
# Debian/Ubuntu
sudo apt-get install -y libzmq3-dev

# CentOS/RHEL
sudo yum install -y zeromq-devel
```

#### Protocol Buffers 支持

```bash
# Debian/Ubuntu
sudo apt-get install -y libprotobuf-dev protobuf-compiler

# CentOS/RHEL
sudo yum install -y protobuf-devel protobuf-compiler
```

#### Prometheus 支持

```bash
# 需要从源码安装或使用第三方软件包
# https://github.com/jupp0r/prometheus-cpp
```

## 3. 获取源码

### 3.1 通过 Git 克隆

```bash
git clone https://github.com/bersler/OpenLogReplicator.git
cd OpenLogReplicator
```

### 3.2 下载发布版本

```bash
# 如果不想使用 Git，可以从 GitHub 下载最新发布版
wget https://github.com/bersler/OpenLogReplicator/archive/refs/tags/v1.8.5.tar.gz
tar -xzf v1.8.5.tar.gz
cd OpenLogReplicator-1.8.5
```

## 4. 编译步骤

### 4.1 使用 Docker 构建（推荐）

使用 Docker 是最简单的构建方式，可以避免环境差异问题：

```bash
# 克隆 Docker 构建仓库
git clone https://github.com/bersler/OpenLogReplicator-docker
cd OpenLogReplicator-docker

# 构建 Docker 镜像
bash build.sh
```

### 4.2 手动编译

在项目根目录下执行：

```bash
# 创建并进入构建目录
mkdir build
cd build

# 配置构建选项（根据需要调整）
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DWITH_OCI=ON \              # 启用 Oracle OCI 支持
  -DWITH_RDKAFKA=ON \          # 启用 Kafka 支持
  -DWITH_PROTOBUF=ON \         # 启用 Protocol Buffers 支持
  -DWITH_ZEROMQ=ON \           # 启用 ZeroMQ 支持
  -DWITH_PROMETHEUS=OFF        # 启用 Prometheus 支持

# 编译
make -j$(nproc)

# 安装（可选）
sudo make install
```

### 4.3 编译选项说明

| 选项 | 说明 | 默认值 |
|------|------|--------|
| WITH_OCI | 启用 Oracle OCI 支持，用于在线模式 | OFF |
| WITH_RDKAFKA | 启用 Kafka 支持 | OFF |
| WITH_PROTOBUF | 启用 Protocol Buffers 支持 | OFF |
| WITH_ZEROMQ | 启用 ZeroMQ 支持（需要 WITH_PROTOBUF=ON） | OFF |
| WITH_PROMETHEUS | 启用 Prometheus 监控支持 | OFF |
| CMAKE_BUILD_TYPE | 构建类型（Debug/Release/RelWithDebInfo） | Release |

## 5. 配置文件

OpenLogReplicator 使用 JSON 格式的配置文件。一个基本的配置示例：

```json
{
  "version": "1.8.5",
  "log-level": 2,
  "source": [
    {
      "alias": "oracle-source",
      "name": "ORCLPDB",
      "reader": {
        "type": "offline",
        "disable-checks": 0,
        "start-scn": 0
      },
      "memory": {
        "min-mb": 32,
        "max-mb": 1024
      },
      "state": {
        "type": "disk",
        "path": "checkpoint"
      },
      "format": {
        "type": "json"
      }
    }
  ],
  "target": [
    {
      "alias": "file-target",
      "source": "oracle-source",
      "writer": {
        "type": "file",
        "output": "output/oracle-data.json",
        "new-line": 1,
        "max-file-size": 1073741824
      }
    }
  ]
}
```

将上述配置保存为 `config.json` 或任意名称，用于启动程序。

### 5.1 配置文件说明

1. **source** 部分定义数据源：
   - `reader.type`: 读取器类型（online/offline/batch）
   - `start-scn`: 起始 SCN，0 表示从最早可用的重做日志开始

2. **target** 部分定义输出目标：
   - `writer.type`: 输出类型（file/kafka/zeromq/network/discard）
   - 其他配置根据输出类型不同而变化

## 6. 运行程序

编译完成后，在构建目录中会生成可执行文件。

### 6.1 基本运行

```bash
# 指定配置文件路径
./OpenLogReplicator -f /path/to/config.json

# 查看版本信息
./OpenLogReplicator -v
```

### 6.2 命令行参数

| 参数 | 说明 |
|------|------|
| -v, --version | 显示版本信息并退出 |
| -f, --file CONFIG | 指定配置文件路径 |
| -p, --process PROCESSNAME | 设置进程名称 |
| -r, --root | 允许以 root 用户运行（不推荐） |

### 6.3 以服务方式运行

可以创建 systemd 服务文件实现开机自启动和服务管理：

```bash
# 创建服务文件
sudo nano /etc/systemd/system/openlogreplicator.service
```

内容如下：

