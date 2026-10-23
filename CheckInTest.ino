#include <M5Cardputer.h>

#include "config.h"
#include "profile.h"
#include "checkin.h"
#include "net.h"
#include "ui.h"

enum class Screen { Main, Message, WiFiList, WiFiSsid, WiFiPass, Reminder, PowerSave, Settings };
static Screen g_screen = Screen::Main;
static unsigned long g_msgSince = 0;
static unsigned long g_lastTick = 0;
static String g_dateKey;  // 当前本地日期，跨天（0点）时用于重置打卡状态
static bool g_timeWasValid = false;  // NTP 是否已完成首次同步
static String g_remindedDate;  // 已触发过 18 点提醒的日期（每天只提醒一次）
// 省电模式状态
static Screen g_prevScreen = Screen::Main;   // 进省电前所在界面（退出时恢复）
static uint8_t g_prevBright = 255;           // 进省电前的亮度
static unsigned long g_sleepTick = 0;        // 省电界面时间刷新
static unsigned long g_lastActivity = 0;     // 最后一次按键/操作时间
// 设置页状态
static int g_setSel = 0;          // 当前选中项
static Settings g_setOrig;        // 进入设置时的副本（tab 取消时恢复）

static std::vector<String> g_apList;
static int g_wifiSel = 0;
static String g_cfgSsid;
static String g_cfgInput;
static bool g_cfgFromMain = false;

static void showMessage(const String& title, const String& body, bool ok) {
  uiDrawMessage(title, body, ok);
  g_screen = Screen::Message;
  g_msgSince = millis();
}

static void showMain() {
  uiDrawMain();
  g_screen = Screen::Main;
  g_lastActivity = millis();  // 返回主界面，重新计算空闲计时
}

// 进入省电模式：亮度降到 10%，整屏只显示时间
static void enterPowerSave() {
  g_prevScreen = g_screen;
  g_prevBright = M5Cardputer.Display.getBrightness();
  uint8_t dim = g_prevBright / 10;      // 10% 亮度
  if (dim < 3) dim = 3;                 // 保底可见
  M5Cardputer.Display.setBrightness(dim);
  uiDrawPowerSave(timeValid() ? formatNow("%H:%M:%S") : "--:--:--");
  g_screen = Screen::PowerSave;
  g_sleepTick = millis();
  Serial.println("[power] 进入省电模式");
}

// 退出省电：恢复亮度，回到进入前的界面
static void exitPowerSave() {
  M5Cardputer.Display.setBrightness(g_prevBright);
  switch (g_prevScreen) {
    case Screen::WiFiList:
      g_screen = Screen::WiFiList;
      uiDrawWifiList(g_wifiSel, "", g_apList);
      break;
    case Screen::WiFiSsid:
      g_screen = Screen::WiFiSsid;
      uiDrawWifiInput("", g_cfgInput, false, "");
      break;
    case Screen::WiFiPass:
      g_screen = Screen::WiFiPass;
      uiDrawWifiInput(g_cfgSsid, g_cfgInput, true, "");
      break;
    default:  // Main / Message / Reminder 统一回主界面
      showMain();
      break;
  }
  g_lastActivity = millis();
  Serial.println("[power] 退出省电模式");
}

// 18 点未打卡提醒：全屏只显示提醒语 + 扬声器响两声（Speaker.tone，同打卡成功音效），回车直接打卡
static void showReminder() {
  uiDrawReminder();
  uiReminderBeep();  // 约 1.3 秒：0.5s 声 + 0.3s 间隔 + 0.5s 声
  g_screen = Screen::Reminder;
  g_remindedDate = formatNow("%Y-%m-%d");
  Serial.println("[checkin] 18:00 未打卡提醒已触发");
}

static void enterAfterConnect() {
  if (!profileComplete()) {
    showMessage("请填写资料", String("手机连同一 WiFi，打开 ") + MDNS_URL + " 或看屏下方 IP", false);
  } else {
    showMain();
  }
}

// ---- 机上 WiFi 配置 ----

static void redrawWifiList(const String& err = "") {
  g_screen = Screen::WiFiList;
  uiDrawWifiList(g_wifiSel, err, g_apList);
}

static void openWifiConfig(bool fromMain, const String& err = "") {
  g_cfgFromMain = fromMain;
  uiDrawScanning();
  g_apList = scanApList();
  g_wifiSel = 0;
  redrawWifiList(err);
}

static void finishConnect(const String& ssid, const String& pass) {
  uiDrawConnecting(ssid);
  if (tryConnect(ssid, pass)) {
    if (ssid != effectiveSsid()) backupWifiCreds();
    g_profile.ssid = ssid;
    g_profile.pass = pass;
    profileSave();
    g_usedSsid = ssid;
    if (g_servicesStarted) {
      showMessage("已切换 WiFi", "连接成功，重启生效", true);
      delay(1200);
      ESP.restart();
    }
    startStaServices();
    enterAfterConnect();
  } else {
    g_screen = Screen::WiFiPass;
    uiDrawWifiInput(ssid, "", true, "连接失败：密码错或信号弱");
  }
}

static void wifiListKeys() {
  auto& ks = M5Cardputer.Keyboard.keysState();
  int total = (int)g_apList.size() + 1;
  bool moved = false;
  for (char c : ks.word) {
    if (c == 'r' || c == 'R') {                       // 重新扫描
      uiDrawScanning();
      std::vector<String> fresh = scanApList();
      if (fresh.size() > 0) g_apList = fresh;         // 扫到空则保留旧列表
      if (g_wifiSel > (int)g_apList.size()) g_wifiSel = 0;
      redrawWifiList();
      return;
    } else if (c == 'w' || c == 'W' || c == ';') {           // 上
      g_wifiSel = (g_wifiSel + total - 1) % total;
      moved = true;
    } else if (c == 's' || c == 'S' || c == '.') {    // 下
      g_wifiSel = (g_wifiSel + 1) % total;
      moved = true;
    } else if (c == ',') {                            // 上翻页
      g_wifiSel -= UI_LIST_WIN;
      if (g_wifiSel < 0) g_wifiSel = 0;
      moved = true;
    } else if (c == '/') {                            // 下翻页
      g_wifiSel += UI_LIST_WIN;
      if (g_wifiSel > total - 1) g_wifiSel = total - 1;
      moved = true;
    }
  }
  if (ks.tab) {
    if (g_cfgFromMain || g_servicesStarted) showMain();
    return;
  }
  if (ks.enter) {
    g_cfgInput = "";
    if (g_wifiSel < (int)g_apList.size()) {
      g_cfgSsid = g_apList[g_wifiSel];
      g_screen = Screen::WiFiPass;
      uiDrawWifiInput(g_cfgSsid, g_cfgInput, true, "");
    } else {
      g_cfgSsid = "";
      g_screen = Screen::WiFiSsid;
      uiDrawWifiInput("", g_cfgInput, false, "");
    }
    return;
  }
  if (moved) uiDrawWifiList(g_wifiSel, "", g_apList);
}

static void wifiSsidKeys() {
  auto& ks = M5Cardputer.Keyboard.keysState();
  if (ks.tab) {
    redrawWifiList();
    return;
  }
  bool changed = false;
  for (char c : ks.word) {
    if (c >= 32 && c < 127 && g_cfgInput.length() < 32) {
      g_cfgInput += c;
      changed = true;
    }
  }
  if (ks.del && g_cfgInput.length() > 0) {
    g_cfgInput.remove(g_cfgInput.length() - 1);
    changed = true;
  }
  if (ks.enter) {
    if (g_cfgInput.length() == 0) {
      uiDrawWifiInput("", g_cfgInput, false, "SSID 不能为空");
      return;
    }
    g_cfgSsid = g_cfgInput;
    g_cfgInput = "";
    g_screen = Screen::WiFiPass;
    uiDrawWifiInput(g_cfgSsid, g_cfgInput, true, "");
    return;
  }
  if (changed) uiDrawWifiInput("", g_cfgInput, false, "");
}

static void wifiPassKeys() {
  auto& ks = M5Cardputer.Keyboard.keysState();
  if (ks.tab) {
    redrawWifiList();
    return;
  }
  bool changed = false;
  for (char c : ks.word) {
    if (c >= 32 && c < 127 && g_cfgInput.length() < 64) {
      g_cfgInput += c;
      changed = true;
    }
  }
  if (ks.del && g_cfgInput.length() > 0) {
    g_cfgInput.remove(g_cfgInput.length() - 1);
    changed = true;
  }
  if (ks.enter) {
    finishConnect(g_cfgSsid, g_cfgInput);
    return;
  }
  if (changed) uiDrawWifiInput(g_cfgSsid, g_cfgInput, true, "");
}

// ---- 设置页 ----

static void openSettings() {
  g_setOrig = g_settings;
  g_setSel = 0;
  g_screen = Screen::Settings;
  uiDrawSettings(g_setSel);
}

static void settingsAdjust(int dir) {
  switch (g_setSel) {
    case 0: {  // 亮度 10-100%，步进 10（即时生效）
      int b = (int)g_settings.bright + dir * 10;
      if (b < 10) b = 10;
      if (b > 100) b = 100;
      g_settings.bright = (uint8_t)b;
      uiApplyBrightness();
      break;
    }
    case 1: {  // 空闲档位 5/10/30/60/300 秒
      int i = (int)g_settings.idleIdx + dir;
      if (i < 0) i = 0;
      if (i > 4) i = 4;
      g_settings.idleIdx = (uint8_t)i;
      break;
    }
    case 2:  // 打卡后关 WiFi
      g_settings.wifiOff = !g_settings.wifiOff;
      break;
  }
}

static void settingsKeys() {
  auto& ks = M5Cardputer.Keyboard.keysState();
  if (ks.enter) {
    settingsSave();
    showMain();
    return;
  }
  if (ks.tab) {  // 取消：恢复进入设置前的值
    g_settings = g_setOrig;
    uiApplyBrightness();
    showMain();
    return;
  }
  bool changed = false;
  for (char c : ks.word) {
    if (c == ';' || c == 'w' || c == 'W') {       // 上一项
      g_setSel = (g_setSel + 2) % 3;
      changed = true;
    } else if (c == '.' || c == 's' || c == 'S') { // 下一项
      g_setSel = (g_setSel + 1) % 3;
      changed = true;
    } else if (c == ',' || c == 'a' || c == 'A') { // 左：值减小
      settingsAdjust(-1);
      changed = true;
    } else if (c == '/' || c == 'd' || c == 'D') { // 右：值增大
      settingsAdjust(+1);
      changed = true;
    }
  }
  if (changed) uiDrawSettings(g_setSel);
}

// ---- 打卡 ----

static void doCheckin() {
  if (!profileComplete()) {
    showMessage("资料未填", String("请手机打开 ") + MDNS_URL + " 填写资料", false);
    return;
  }
  if (!timeValid()) {  // 必须先于日期比较：时间未同步时 formatNow 是 1970 年
    showMessage("时间未同步", "请检查网络后重试", false);
    return;
  }
  String today = formatNow("%Y-%m-%d");
  if (g_profile.lastDate == today) {
    showMessage("无需打卡", "今天已经打卡过了", true);
    return;
  }

  // WiFi 被省电关闭或掉线时，打卡前自动恢复连接
  if (WiFi.status() != WL_CONNECTED) {
    uiDrawConnecting(effectiveSsid());
    if (!wifiConnectSta()) {
      showMessage("WiFi 未连接", "请按 w 重新配网后重试", false);
      return;
    }
    startStaServices();
  }

  uiDrawChecking();
  CheckinResult r = doCheckinPost();
  if (r.ok) {
    g_profile.lastDate = today;
    profileSave();
    checkinRecordWrite(today, formatNow("%H:%M:%S"));  // 独立打卡记录，仅当天有效
    if (g_settings.wifiOff) wifiOffForPowerSave();     // 打卡成功后关闭 WiFi 省电
    uiBeep(true);
    showMessage("打卡成功", r.msg, true);
  } else {
    uiBeep(false);
    String body = "HTTP " + String(r.code) + "\n" + r.msg;
    showMessage("打卡失败", body, false);
  }
}

void setup() {
  Serial.begin(115200);
  uiInit();
  profileLoad();
  settingsLoad();     // 需在 profileLoad 之后（NVS/SD 已就绪）
  uiApplyBrightness();
  ntpBegin();

  if (hasWifiCreds()) {
    uiDrawConnecting(effectiveSsid());
    if (wifiConnectSta()) {
      startStaServices();
      enterAfterConnect();
      return;
    }
  }
  openWifiConfig(false, hasWifiCreds() ? "自动连接失败" : "首次使用，请选 WiFi");
}

void loop() {
  M5Cardputer.update();
  // 键盘与 Go 键（顶部 G0 按钮）统一检测，任何操作都刷新“最后活动时间”
  bool keyEvent = M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed();
  bool goPressed = M5Cardputer.BtnA.wasPressed();
  if (keyEvent || goPressed) g_lastActivity = millis();
  bool powerEntered = false;
  if (goPressed && g_screen != Screen::PowerSave) {  // Go 键随时进入省电模式
    enterPowerSave();
    powerEntered = true;
  }
  netLoop();

  // 时间同步与跨天处理
  if (timeValid()) {
    String today = formatNow("%Y-%m-%d");
    int hhmmss = formatNow("%H%M%S").toInt();
    if (!g_timeWasValid) {
      // 开机 NTP 首次同步完成：应用打卡记录（仅当天有效）并重绘主界面。
      // 否则开机瞬间主界面是用 1970 年日期画的，会误显示“未打卡”。
      g_timeWasValid = true;
      g_dateKey = today;
      String rec = checkinRecordDate();
      if (rec == today) {
        if (g_profile.lastDate != today) {  // 有当天记录但存档缺失 -> 回填
          g_profile.lastDate = today;
          profileSave();
        }
      } else if (rec.length() > 0) {
        checkinRecordClear();  // 记录不是今天的：仅当天有效，作废
        if (g_profile.lastDate == rec) {
          g_profile.lastDate = "";
          profileSave();
        }
      }
      if (g_screen == Screen::Main) uiDrawMain();
    } else if (today != g_dateKey) {
      // 跨天（本地 0 点）：删除当天打卡记录文件（仅当天有效），
      // lastDate 只清内存不落盘——重新上电读回的日期与今天比必然不等，判定一致。
      g_dateKey = today;
      g_profile.lastDate = "";
      checkinRecordClear();
      g_remindedDate = "";  // 新的一天，允许再次触发 18 点提醒
      Serial.printf("[checkin] 跨天重置打卡状态 -> %s\n", today.c_str());
      if (g_screen == Screen::Main) uiDrawMain();
    }

    // 打卡后关 WiFi 的重启恢复：同步时间后若当天已打卡，再次关闭 WiFi
    if (g_settings.wifiOff && WiFi.status() == WL_CONNECTED &&
        g_profile.lastDate == today) {
      wifiOffForPowerSave();
    }

    // 18:00 仍未打卡 -> 全屏提醒 + 扬声器响两声（每天一次；不打断配网输入）
    if (g_profile.lastDate != today && g_remindedDate != today &&
        hhmmss >= 180000 &&
        (g_screen == Screen::Main || g_screen == Screen::Message)) {
      showReminder();
    }

    // 21:00～次日 06:00 且已打卡：主界面空闲（设置档位，默认 10 秒）无操作自动进入省电
    if (g_screen == Screen::Main && g_profile.lastDate == today &&
        (hhmmss >= 210000 || hhmmss < 60000) &&
        millis() - g_lastActivity >= (unsigned long)settingsIdleSec() * 1000UL) {
      enterPowerSave();
    }
  }

  switch (g_screen) {
    case Screen::Main: {
      if (millis() - g_lastTick >= 1000) {
        g_lastTick = millis();
        uiTickMain();
      }
      if (keyEvent) {
        auto& ks = M5Cardputer.Keyboard.keysState();
        if (ks.enter) {
          doCheckin();
        } else {
          for (char c : ks.word) {
            if (c == 'w' || c == 'W') {
              openWifiConfig(true);
              return;
            } else if (c == 'i' || c == 'I') {
              openSettings();  // i 键进入设置（仅主界面生效）
              return;
            }
          }
        }
      }
      break;
    }
    case Screen::Message: {
      if (keyEvent || millis() - g_msgSince > 4000) {
        showMain();
      }
      break;
    }
    case Screen::Reminder: {
      // 回车完成打卡；其它键返回主界面
      if (keyEvent) {
        auto& ks = M5Cardputer.Keyboard.keysState();
        if (ks.enter) {
          doCheckin();
        } else {
          showMain();
        }
      }
      break;
    }
    case Screen::PowerSave: {
      // 每秒刷新时间
      if (millis() - g_sleepTick >= 1000) {
        g_sleepTick = millis();
        if (timeValid()) uiDrawPowerSave(formatNow("%H:%M:%S"));
      }
      // 任意键或 Go 键退出（用 Go 进入的这一轮不算退出）
      if (!powerEntered && (keyEvent || goPressed)) {
        exitPowerSave();
      }
      break;
    }
    case Screen::Settings: {
      if (keyEvent) {
        settingsKeys();
      }
      break;
    }
    case Screen::WiFiList: {
      if (keyEvent) {
        wifiListKeys();
      }
      break;
    }
    case Screen::WiFiSsid: {
      if (keyEvent) {
        wifiSsidKeys();
      }
      break;
    }
    case Screen::WiFiPass: {
      if (keyEvent) {
        wifiPassKeys();
      }
      break;
    }
  }
}
