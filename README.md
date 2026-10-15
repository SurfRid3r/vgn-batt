# vgn-batt

查询 VGN / Compx 方案 2.4G 接收器所连鼠标的电量，macOS 命令行工具 + Hammerspoon 常驻监控。

```bash
$ vgnbatt
鼠标电量: 100%  (2.4G 无线, 4226 mV)
```

## 功能

- **电量查询**：电量百分比、充电状态、电池电压（mV）
- **Hammerspoon 监控**：接收器插入自动开始轮询，拔出自动停止；
  电量自适应查询间隔（电量越足查得越稀）；电量低于阈值时通知 + 屏幕提醒
- **零依赖**：仅使用系统框架 IOKit / CoreFoundation；只读设备状态，不修改任何配置

## 支持设备

采用 Compx / Nordic 52840 方案 2.4G 接收器的鼠标（接收器厂商名显示为 "Compx"）：

| 品牌 | 型号（接收器 PID） |
|---|---|
| VGN | F1 Pro (`f503`)、F1 SE、F2 Pro Max (`fb3e`) |
| VXE | R1 Pro Max (`f58a`)、R1 SE+ (`f58e`) |
| Zaopin | Z2 Mini (`f524`) |
| Scyrox | V8 (`f5f7`) |
| Dareu | A950 Air、A950 Wing |
| G-Wolves | Lycan |

 VID/PID 不同的同方案设备，改 `vgnbatt.c` 顶部的 `VENDOR` / `PRODUCT` 宏重新编译即可。

## 编译

要求：macOS + Xcode Command Line Tools（`xcode-select --install`）。

```bash
make                # 编译 vgnbatt 和 probe
make install        # 安装 vgnbatt 到 ~/.hammerspoon/bin/
make install-hs     # 同时部署 Hammerspoon 模块到 ~/.hammerspoon/
make clean
```

也可以从 GitHub Releases / Actions artifact 下载预编译的 universal binary（arm64 + x86_64），无需本地编译。

## 使用

### 命令行

```bash
$ vgnbatt
鼠标电量: 100%  (2.4G 无线, 4226 mV)
```

| 退出码 | 含义 |
|---|---|
| `0` | 查询成功 |
| `3` | 未找到接收器 |
| `4` | 设备无响应（鼠标深度睡眠，移动鼠标后重试） |

输出格式固定为 `电量: N%  (状态, N mV)`，便于脚本解析。

### Hammerspoon

`make install-hs` 后，在 `~/.hammerspoon/init.lua` 中添加：

```lua
vgnbatt = require("vgnbatt")
```

重载配置即可。行为细节：

| 状态 | 查询间隔 |
|---|---|
| 电量 ≥ 70% | 1 小时 |
| 40–69% | 30 分钟 |
| 30–39% | 10 分钟 |
| < 30% | 5 分钟 |
| 充电中 | 10 分钟 |
| 无响应（睡眠） | 60s 起指数退避，上限 10 分钟 |

- 电量 < 30% 且未充电：🪫 通知中心 + 红色屏幕提示，每小时最多提醒一次；
  插入充电线或回升至 35% 以上即解除
- 手动查询：`hs -c 'vgnbatt.query()'`

## 工作原理

此类接收器不实现标准 HID Battery usage page，macOS 系统工具（`ioreg`、
`system_profiler`、`hidutil`）无法读取电量，需要向接收器的 vendor HID
collection（usagePage `0xFF02` / usage `0x0002`）发送私有协议报文：

- 请求（OUT report 8，17 字节）：`08 04 … 49`，末字节为校验
  `(85 - Σ[0..14] - reportId) mod 256`，子命令 `0x04` = BatteryLevel
- 响应（IN report 8，17 字节）：`[6]` 电量 %、`[7]` 充电标志、
  `[8..9]` 电压 mV（大端）、全报文 `Σ mod 256 = 0x55`

**macOS 注意**：`IOHIDDeviceSetReport` 的 data 参数必须包含 report ID
前缀字节（17 字节整包传入）。按 Windows hidapi 习惯剥离前缀传 16 字节时
调用同样返回成功，但固件不响应。

`probe.c` 是配套的协议探测工具：枚举 HID elements、扫描 GET_REPORT、
轮询监听 input report，用于排查新设备。

## 致谢

协议来自 [VGN 官方网页驱动](https://hub.vgnlab.com/) 逆向，
并与 [Fan4Metal/mouse_tray](https://github.com/Fan4Metal/mouse_tray) 的
`nordic52` 驱动交叉验证。

## License

MIT
