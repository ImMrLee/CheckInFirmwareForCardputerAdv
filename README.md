# CheckInFirmwareForCardputerAdv

存储适用于 CardputerAdv 的打卡固件的源代码，仅供个人学习使用。

固件运行在 **M5Cardputer**（ESP32-S3）上，一键连接 WiFi、通过网页填写资料、按回车完成每日打卡上报。当前版本 **v1.0a**。

## 核心功能

### 📌 每日打卡
- 主界面按 `回车` 立即向打卡服务器发起 HTTPS 请求（TLS 证书固定校验）
- 上报内容：姓名、手机号、年龄（按生日自动计算）、性别、紧急联系人、打卡时间、地址
- 当天已打卡会明确提示"今日已打卡 ✓"，避免重复上报
- 打卡成功后状态**双存储持久化**（NVS + SD 卡 `/CheckInInfo/profile.txt`），重启不丢失
- 本地 0 点跨天自动重置打卡状态，第二天可正常打卡

### 📶 WiFi 连接管理
- 屏上直接配网：扫描周边 AP → 键盘选择/手动输入 → 输密码连接（`w` 键随时发起）
- 主凭据连接失败时**自动回退**到上一份可用凭据，改错密码不会"变砖"
- WiFi 凭据同样双存储（NVS + SD 卡 `wifi.txt`），并保留备份文件 `wifi_bk.txt`

### 🌐 内置 Web 配置页
- STA 模式 + mDNS，手机连同一 WiFi 后访问 **http://checkin.local** 或屏幕下方 IP
- 网页表单修改：WiFi（含扫描候选列表）、姓名、手机号、生日、性别、紧急联系人、地址
- 保存后自动重启生效

### ⏰ 时间与时区
- NTP 自动校时（`ntp.aliyun.com` / `pool.ntp.org`），时区 `CST-8`（UTC+8）
- 时间未同步时禁止打卡，避免把错误时间上报给服务器
- 主界面每秒刷新时钟，并按时段/生日显示问候语（如"早上好"、"生日快乐"）

### 🖥️ 中文 UI
- 240×240 屏全中文界面（efontCN 字库）：主界面 / 消息页 / WiFi 列表 / SSID 输入 / 密码输入
- 打卡成功/失败均有蜂鸣提示 + 明确的错误信息（HTTP 状态码与服务器返回内容）

## 目录结构

```
CheckInTest/
├── CheckInTest.ino   # 主程序：5 个界面状态机 + 键盘输入 + 跨天检测
├── config.h          # 常量配置：WiFi、mDNS、NTP、时区、服务器地址与证书
├── profile.h         # 资料与打卡状态的 NVS + SD 双存储读写、WiFi 凭据备份
├── net.h             # WiFi 连接/扫描 + Web 配置页（80 端口）
├── checkin.h         # NTP/时区、年龄与问候逻辑、打卡 HTTP POST
└── ui.h              # 屏幕绘制与蜂鸣器提示
```

## 编译与烧录

依赖：`arduino-cli` + M5Stack 板卡包（中国镜像，无需代理）。

```bash
# 1. 添加板卡源（文件 → 首选项对应的 CLI 配置）
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

1. 首次开机：屏幕扫描 WiFi → 选择网络并输入密码
2. 连接成功后，手机访问 `http://checkin.local` 填写个人资料
3. 回到主界面按 `回车` 打卡；按 `w` 可重新配网

## 免责

仅供个人学习使用，由此产生的任何后果自负。
