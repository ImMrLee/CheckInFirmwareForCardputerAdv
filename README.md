# CheckInFirmwareForCardputerAdv

存储适用于 CardputerAdv 的打卡固件的源代码，仅供个人学习使用。

固件运行在 **M5Cardputer / Cardputer-Adv**（ESP32-S3）上，一键连接 WiFi、通过网页填写资料、按回车完成每日打卡上报。当前版本 **v1.2a**。

## 核心功能

### 📌 每日打卡
- 主界面按 `回车` 立即向打卡服务器发起 HTTPS 请求（TLS 证书固定校验）
- 上报内容：姓名、手机号、年龄（按生日自动计算）、性别、紧急联系人、打卡时间、地址
- 当天已打卡会明确提示"今日已打卡 ✓"，避免重复上报
- 打卡状态**双存储持久化**（NVS + SD 卡 `/CheckInInfo/profile.txt`），重启不丢失
- 独立打卡记录 `/CheckInInfo/checkin.txt`（**仅当天有效**）：过期自动作废，NTP 同步后自动校正状态
- 本地 0 点跨天自动重置打卡状态，第二天可正常打卡

### ⏰ 时间与时区
- NTP 自动校时（`ntp.aliyun.com` / `pool.ntp.org`），时区 `CST-8`（UTC+8）
- 时间未同步时主界面显示"正在同步时间 …"，禁止打卡，避免上报错误时间
- 同步完成后自动刷新主界面；时钟每秒刷新，按时段/生日显示问候语（如"早上好"、"生日快乐"）

### 🔔 18:00 未打卡提醒
- 当天 18:00 仍未打卡时，扬声器响两声（**0.5 秒 / 间隔 0.3 秒 / 0.5 秒**），每天仅一次
- 屏幕全屏只显示"**不要忘记打卡哦**"
- 按 `回车` 直接进入打卡流程，按其它键返回主界面

### 🌙 省电模式
- 按机器顶部的 **Go（G0）键**随时进入：亮度降为 **10%**，整屏只显示时间，每秒刷新
- 按任意键退出，恢复原亮度并回到进入前的界面（配网输入内容不丢）
- **21:00 ～ 次日 06:00** 且**当天已打卡**时，主界面**空闲超时**（档位可设 5/10/30/60/300 秒，默认 10 秒）自动进入省电模式（未打卡保持常亮）
- **打卡成功后可自动关闭 WiFi**（设置项，默认关）进一步省电：之后按回车打卡或按 `w` 配网时自动重连；重启后同步完时间若发现当天已打卡会再次关闭
- 主界面实时显示 WiFi 状态：已连接（SSID + 配置页）/ 已关闭 / 未连接

### ⚙️ 设置界面（主界面按 `i`）
| 设置项 | 调整 | 默认 |
|---|---|---|
| 亮度 | 10% ～ 100%，步进 10%（即时生效） | 100% |
| 空闲时间 | 5 / 10 / 30 / 60 / 300 秒档位 | 10 秒 |
| 打卡后关WiFi | 开 / 关 | 关 |

- 按键与 WiFi 列表同构：`;`上 `.`下 `，`左 `/`右（`w/s/a/d` 等价别名），回车保存、tab 取消
- 设置存 NVS + SD 卡 `/CheckInInfo/settings.txt` 双份，开机自动应用

### 📶 WiFi 连接管理
- 屏上直接配网：扫描周边 AP → 键盘选择/手动输入 → 输密码连接（`w` 键随时发起），列表页按 **`r`** 随时重新扫描
- 主凭据连接失败时**自动回退**到上一份可用凭据，改错密码不会"变砖"
- WiFi 凭据同样双存储（NVS + SD 卡 `wifi.txt`），并保留备份文件 `wifi_bk.txt`

### 🌐 内置 Web 配置页
- STA 模式 + mDNS，手机连同一 WiFi 后访问 **http://checkin.local** 或屏幕下方 IP
- 网页表单修改：WiFi（含扫描候选列表）、姓名、手机号、生日、性别、紧急联系人、地址
- 保存后自动重启生效

### 🖥️ 中文 UI
- 240×240 屏全中文界面（efontCN 字库）：主界面 / 消息页 / WiFi 列表 / SSID 输入 / 密码输入 / 打卡提醒页 / 省电时钟页
- 打卡成功/失败均有扬声器音效 + 明确的错误信息（HTTP 状态码与服务器返回内容）

## 目录结构

```
CheckInTest/
├── CheckInTest.ino   # 主程序：界面状态机 + 键盘/Go键输入 + 跨天检测 + 提醒与省电 + 设置页
├── config.h          # 常量配置：WiFi、mDNS、NTP、时区、服务器地址与证书
├── profile.h         # 资料/打卡状态/设置的 NVS + SD 双存储、打卡记录（仅当天有效）、WiFi 凭据备份
├── net.h             # WiFi 连接/扫描/按需开关 + Web 配置页（80 端口）
├── checkin.h         # NTP/时区、年龄与问候逻辑、打卡 HTTP POST
└── ui.h              # 屏幕绘制与扬声器音效
```

## 编译与烧录

依赖：`arduino-cli` + M5Stack 板卡包（中国镜像，无需代理）。

```bash
# 1. 添加板卡源
arduino-cli config set board_manager.additional_urls \
  https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index_cn.json
arduino-cli core update-index
arduino-cli core install m5stack:esp32@3.3.10

# 2. 安装依赖库
arduino-cli lib install M5Cardputer
arduino-cli lib install ArduinoJson

# 3. 编译
arduino-cli compile \
  -b "m5stack:esp32:m5stack_cardputer:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi" \
  /path/to/CheckInTest

# 4. 烧录（先用 arduino-cli board list 查端口）
arduino-cli upload \
  -b "m5stack:esp32:m5stack_cardputer:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi" \
  -p /dev/ttyACM0 \
  /path/to/CheckInTest
```

## 使用说明

1. 首次开机：屏幕扫描 WiFi → 选择网络并输入密码（列表页 `r` 可重新扫描）
2. 连接成功后，手机访问 `http://checkin.local` 填写个人资料
3. 回到主界面按 `回车` 打卡；按 `w` 重新配网；按 `i` 进入设置
4. 18:00 未打卡会有两声提醒；按顶部 **Go 键**可随时进入省电时钟模式，按任意键退出

## 免责

仅供个人学习使用，由此产生的任何后果自负。
