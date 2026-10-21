#include <M5Cardputer.h>

#include "config.h"
#include "profile.h"
#include "checkin.h"
#include "net.h"
#include "ui.h"

enum class Screen { Main, Message, WiFiList, WiFiSsid, WiFiPass };
static Screen g_screen = Screen::Main;
static unsigned long g_msgSince = 0;
static unsigned long g_lastTick = 0;
static String g_dateKey;  // 当前本地日期，跨天（0点）时用于重置打卡状态

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
    if (c == 'w' || c == 'W' || c == ';') {           // 上
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

// ---- 打卡 ----

static void doCheckin() {
  if (!profileComplete()) {
    showMessage("资料未填", String("请手机打开 ") + MDNS_URL + " 填写资料", false);
    return;
  }
  String today = formatNow("%Y-%m-%d");
  if (g_profile.lastDate == today) {
    showMessage("无需打卡", "今天已经打卡过了", true);
    return;
  }
  if (!timeValid()) {
    showMessage("时间未同步", "请检查网络后重试", false);
    return;
  }

  uiDrawChecking();
  CheckinResult r = doCheckinPost();
  if (r.ok) {
    g_profile.lastDate = today;
    profileSave();
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
  netLoop();

  // 跨天检测：本地 0 点后重置打卡状态并刷新主界面状态栏。
  // 这里只清内存、不落盘：重新上电会读回上次打卡日期，
  // 与今天日期比较必然不等，判定结果一致（未打卡）。
  if (timeValid()) {
    String today = formatNow("%Y-%m-%d");
    if (g_dateKey.length() == 0) {
      g_dateKey = today;
    } else if (today != g_dateKey) {
      g_dateKey = today;
      g_profile.lastDate = "";
      Serial.printf("[checkin] 跨天重置打卡状态 -> %s\n", today.c_str());
      if (g_screen == Screen::Main) uiDrawMain();
    }
  }

  switch (g_screen) {
    case Screen::Main: {
      if (millis() - g_lastTick >= 1000) {
        g_lastTick = millis();
        uiTickMain();
      }
      if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        auto& ks = M5Cardputer.Keyboard.keysState();
        if (ks.enter) {
          doCheckin();
        } else {
          for (char c : ks.word) {
            if (c == 'w' || c == 'W') {
              openWifiConfig(true);
              return;
            }
          }
        }
      }
      break;
    }
    case Screen::Message: {
      bool key = M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed();
      if (key || millis() - g_msgSince > 4000) {
        showMain();
      }
      break;
    }
    case Screen::WiFiList: {
      if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        wifiListKeys();
      }
      break;
    }
    case Screen::WiFiSsid: {
      if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        wifiSsidKeys();
      }
      break;
    }
    case Screen::WiFiPass: {
      if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        wifiPassKeys();
      }
      break;
    }
  }
}
