# vgn-batt — VGN/Compx 2.4G 接收器鼠标电量查询 (macOS)

零依赖查询 VGN 蜻蜓系列鼠标（及同方案 Compx 品牌）经 2.4G USB 接收器的电量，
附带 Hammerspoon 常驻监控：动态间隔轮询 + 低电量提醒。

```
$ vgnbatt
鼠标电量: 100%  (2.4G 无线, 4226 mV)
```

- 仅用系统框架 IOKit + CoreFoundation，无第三方库
- 只读设备状态，不改任何配置
- 退出码：`0` 成功 / `3` 接收器不在 / `4` 设备无响应（鼠标睡眠，动一下再试）

## 背景

此类接收器（Compx/Nordic 52840 方案）**不走标准 HID Battery usage page**，
IORegistry 里没有 `BatteryPercent`，系统级工具（`ioreg`/`system_profiler`/`hidutil`）
全部拿不到电量——只能走 vendor collection 的私有协议。

## 协议

协议通过两条独立来源交叉验证：VGN 官方网页驱动（hub.vgnlab.com 的 JS）+
开源项目 [Fan4Metal/mouse_tray](https://github.com/Fan4Metal/mouse_tray)（`drivers/chipset/nordic52.py`）。

### 设备标识

| 项 | 值 |
|---|---|
| USB VID:PID | `3554:f503`（无线接收器；有线模式为 `f502`） |
| 厂商字符串 | Compx |
| HID 接口 | usagePage `0xFF05` / usage 0（vendor 接口，可 open） |
| 协议 collection | usagePage `0xFF02` / usage `0x0002`（在该 vendor 接口内，**非** PrimaryUsage） |
| 报文尺寸 | IN/OUT report 8 均为 17 字节（含 report ID） |

### 查询报文（OUT report 8, 17B）

```
08 04 00 00 00 00 00 00 00 00 00 00 00 00 00 00 49
│  │                                       │      └─ 校验 = (85 - Σ[0..14] - reportId) mod 256
│  └─ 子命令 0x04 = BatteryLevel            └─ 初始占位 0xEF
└─ report ID
```

### 响应报文（IN report 8, 17B）

```
08 04 00 00 00 02 64 00 10 82 00 00 00 00 00 00 51
│  │  │       │  │  │  └──┴─ 电压 mV (大端, 0x1082 = 4226)
│  │  │       │  │  └─ 充电/有线标志 (0 = 2.4G 无线)
│  │  │       │  └─ 电量 % (0x64 = 100)
│  │  │       └─ 状态字节
│  │  └─ 0 = 成功
└─ 子命令回显
全 17 字节 Σ mod 256 = 0x55
```

官方命令集（子命令枚举，来自网页驱动逆向）：
`1` EncryptionData、`2` PCDriverStatus、`3` DeviceOnLine、`4` BatteryLevel、
`5` DongleEnterPair、`6` GetPairState、`7` WriteFlashData …

### ⚠️ macOS 平台差异（关键坑）

`IOHIDDeviceSetReport(dev, type, reportID, data, len)` 的 **data 参数必须带
reportID 前缀字节**（即传 17B `[08, 04, …, 49]`）——Chromium WebHID 的
`hid_connection_mac.cc` 即此形态。按 Windows hidapi 习惯剥前缀传 16B 时
调用返回 `kIOReturnSuccess` 但线上报文缺 reportID，固件不路由、无响应
（kernel 侧 input report 缓存始终为零可证）。

## 文件

| 文件 | 说明 |
|---|---|
| `vgnbatt.c` | 查询器（干净版）：枚举 → open vendor 接口 → 发查询 → 解析打印 |
| `probe.c` | 探测/调试工具：dump HID elements（找 collection）、`listen` 轮询模式、GET_REPORT 扫描 |
| `hammerspoon/vgnbatt.lua` | 常驻监控模块 |

## 编译 & 安装

```bash
make            # clang vgnbatt.c -o vgnbatt -framework IOKit -framework CoreFoundation
sudo make install   # 或: 装到 ~/.hammerspoon/bin/（无需 sudo）
make install-hs     # 同时部署 Hammerspoon 模块到 ~/.hammerspoon/
```

## Hammerspoon 集成

`init.lua` 加入：

```lua
vgnbatt = require("vgnbatt")
```

行为：

- USB 插拔监听接收器（`hs.usb.watcher`），在位即轮询、拔出即停
- **电量自适应轮询**：≥70% 每 1h / 40–69% 每 30min / 30–39% 每 10min /
  <30% 每 5min / 充电中每 10min；查询无响应时 60s 起指数退避（上限 10min）
- **低电量提醒**：<30% 且未充电 → 🪫 通知 + 红色屏幕提示，每 1h 一次；
  插线或回升 ≥35% 即解除
- 手动查询：`hs -c 'vgnbatt.query()'`

## 适用设备（同方案 Compx 接收器，来自 mouse_tray 型号表）

VGN F1 Pro (`f503`)、F1 SE、F2 Pro Max (`fb3e`)、VXE R1 Pro Max (`f58a`)、
R1 SE+ (`f58e`)、Zaopin Z2 Mini (`f524`)、Scyrox V8 (`f5f7`)、
Dareu A950 Air/Wing、G-Wolves Lycan 等——改 `vgnbatt.c` 顶部的
`VENDOR`/`PRODUCT` 宏即可。
