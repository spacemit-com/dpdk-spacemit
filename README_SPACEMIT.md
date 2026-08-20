# dpdk-spacemit

基于 DPDK 官方代码 v25.11.2 移植的 SpacemiT 平台网络数据面开发套件。

## 版本历史

### v0.1
- 完成 SpacemiT K1 DPDK 适配
- 支持 K1 本地 GMAC 以太网控制器
- 支持 Realtek RTL8111H PCIe 千兆网卡

### v0.2
- 完成 SpacemiT K3 DPDK 适配
- 支持 K3 本地 GMAC 以太网控制器

## 支持的硬件

| 平台 | 网卡类型 | 说明 |
|------|----------|------|
| SpacemiT K1 | 板载 GMAC | 使用 DPDK stmmac 驱动，描述符固定 1024 |
| SpacemiT K1 | Realtek RTL8111H | PCIe 千兆网卡 |
| SpacemiT K3 | 板载 GMAC | 使用 DPDK stmmac 驱动，描述符固定 1024 |

## 特性

- 基于 DPDK v25.11.2 官方版本
- 支持 SpacemiT K1 / K3 平台
- 支持 K1 本地 GMAC 与 Realtek RTL8111H PCIe 网卡
- 支持 K3 本地 GMAC TX/RX DMA 描述符数量固定为 1024，不可修改
- 提供标准 DPDK 用户态驱动接口
- 可运行 testpmd 等常用 DPDK 应用

## 构建与安装

### 1. 获取源码

```bash
git clone https://github.com/spacemit-com/dpdk-spacemit.git
cd dpdk-spacemit
```

### 2. 配置与编译

```bash
sudo apt update
sudo apt install -y build-essential clang llvm libelf-dev
sudo apt install -y meson ninja-build pkg-config libnuma-dev
sudo apt install -y python3-pyelftools
sudo apt install -y libpcap-dev m4
sudo apt install -y libbsd-dev
meson setup -Dplatform=spacemit_k1 build    //K1 平台
meson setup -Dplatform=spacemit_k3 build    //K3 平台
cd build
ninja install
ldconfig
```

### 3. 加载内核驱动
```bash
modprobe uio
modprobe k1xmac          //K1 mac驱动
modprobe igb_uio         //r8111H uio驱动
modprobe stmmac_uio      //K3 stmmac_uio驱动
```

### 4. 绑定设备到用户态驱动

只针对pcie接口网卡

```
cd dpdk
./tools/dpdk-devbind.py --bind=uio_pci_generic 0000:01:00.0
```

### 5. 启动testpmd

```
//k3 stmmac网卡
dpdk-testpmd --iova-mode=pa --vdev=net_stmmac0 --vdev=net_stmmac1 -l 0,2,3 --main-lcore=0 -n 4 -- --forward-mode=flowgen --nb-cores=2 -i --txd=1024 --rxd=1024
```