#pragma once

#include <M5Cardputer.h>
#include "config.h"
#include "profile.h"
#include "checkin.h"  // timeValid() / greetingText()

inline int uiScrW() { return M5Cardputer.Display.width(); }
inline int uiScrH() { return M5Cardputer.Display.height(); }

inline void uiInit() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setTextDatum(middle_center);
  M5Cardputer.Speaker.setVolume(128);
}

inline void uiBeep(bool ok) {
  if (ok) {
    M5Cardputer.Speaker.tone(2000, 100);
    delay(130);
    M5Cardputer.Speaker.tone(2600, 140);
  } else {
    M5Cardputer.Speaker.tone(400, 250);
  }
}

// 18 点未打卡提醒音（扬声器，同 uiBeep 的 Speaker.tone 方式）：两声各 0.5 秒，间隔 0.3 秒
inline void uiReminderBeep() {
  M5Cardputer.Speaker.tone(2000, 500);
  delay(800);  // 0.5s 声音 + 0.3s 间隔（tone 为后台播放，第二声发出即完成）
  M5Cardputer.Speaker.tone(2000, 500);
}

// 提醒页：按要求只显示这一行字
inline void uiDrawReminder() {
  auto& d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  d.setFont(&fonts::efontCN_24);
  d.setTextColor(TFT_ORANGE, TFT_BLACK);
  d.drawString("不要忘记打卡哦", uiScrW() / 2, uiScrH() / 2);
}

// 省电模式：整屏只显示时间
inline void uiDrawPowerSave(const String& hhmmss) {
  auto& d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  d.setFont(&fonts::efontCN_24);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.drawString(hhmmss, uiScrW() / 2, uiScrH() / 2);
}

inline void uiDrawConnecting(const String& ssid) {
  auto& d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  d.setTextColor(TFT_CYAN, TFT_BLACK);
  d.setFont(&fonts::efontCN_16);
  d.drawString("正在连接 WiFi …", uiScrW() / 2, uiScrH() / 2 - 10);
  d.setFont(&fonts::efontCN_12);
  d.setTextColor(TFT_DARKGREY, TFT_BLACK);
  d.drawString(ssid, uiScrW() / 2, uiScrH() / 2 + 16);
}

// 全屏消息页，按任意键返回主界面
inline void uiDrawMessage(const String& title, const String& body, bool ok) {
  auto& d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  d.setFont(&fonts::efontCN_24);
  d.setTextColor(ok ? TFT_GREEN : TFT_RED, TFT_BLACK);
  d.drawString(title, uiScrW() / 2, uiScrH() / 2 - 22);
  d.setFont(&fonts::efontCN_12);
  d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  // 按显式换行 + 约 18 字自动折行
  String line, rest = body;
  int y = uiScrH() / 2 + 14;
  while (rest.length() > 0) {
    int nl = rest.indexOf('\n');
    if (nl >= 0) {
      line = rest.substring(0, nl);
      rest = rest.substring(nl + 1);
    } else if (rest.length() > 18) {
      line = rest.substring(0, 18);
      rest = rest.substring(18);
    } else {
      line = rest;
      rest = "";
    }
    d.drawString(line, uiScrW() / 2, y);
    y += 16;
    if (y > uiScrH() - 8) break;
  }
  d.setFont(&fonts::efontCN_12);
  d.setTextColor(TFT_DARKGREY, TFT_BLACK);
  d.drawString("按任意键返回", uiScrW() / 2, uiScrH() - 10);
}

inline void uiDrawTimeBox(const String& hhmmss) {
  auto& d = M5Cardputer.Display;
  int boxW = 150, boxH = 34;
  d.fillRect(uiScrW() / 2 - boxW / 2, 42, boxW, boxH, TFT_BLACK);
  d.setFont(&fonts::efontCN_24);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.drawString(hhmmss, uiScrW() / 2, 42 + boxH / 2);
}

// 主打卡界面（不含每秒刷新的时间框）
inline void uiDrawMain() {
  auto& d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);

  // 时间未同步时不用真实问候（formatNow 还是 1970 年的时段）
  String top = (timeValid() ? greetingText() : String("你好")) + "，" +
               (g_profile.name.length() ? g_profile.name : "朋友");
  d.setFont(&fonts::efontCN_16);
  d.setTextColor(TFT_YELLOW, TFT_BLACK);
  d.drawString(top, uiScrW() / 2, 18);

  uiDrawTimeBox(timeValid() ? formatNow("%H:%M:%S") : "--:--:--");

  d.setFont(&fonts::efontCN_16);
  if (!timeValid()) {
    // 开机 NTP 未同步前不显示打卡状态：否则会用 1970 年日期误判为“未打卡”
    d.setTextColor(TFT_CYAN, TFT_BLACK);
    d.drawString("正在同步时间 …", uiScrW() / 2, 92);
  } else if (!profileComplete()) {
    d.setTextColor(TFT_ORANGE, TFT_BLACK);
    d.drawString("请先填写资料", uiScrW() / 2, 92);
  } else if (g_profile.lastDate == formatNow("%Y-%m-%d")) {
    d.setTextColor(TFT_GREEN, TFT_BLACK);
    d.drawString("已打卡，祝您平安健康", uiScrW() / 2, 92);
  } else {
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.drawString("按 [回车] 打卡", uiScrW() / 2, 92);
  }

  d.setFont(&fonts::efontCN_12);
  d.setTextColor(TFT_DARKGREY, TFT_BLACK);
  d.drawString(String("配置页 ") + MDNS_URL, uiScrW() / 2, uiScrH() - 26);
  d.drawString(WiFi.localIP().toString() + " · 回车打卡 · w改WiFi",
               uiScrW() / 2, uiScrH() - 10);
}

inline void uiTickMain() {
  uiDrawTimeBox(timeValid() ? formatNow("%H:%M:%S") : "--:--:--");
}

inline void uiDrawChecking() {
  auto& d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  d.setFont(&fonts::efontCN_16);
  d.setTextColor(TFT_CYAN, TFT_BLACK);
  d.drawString("打卡中，请稍候 …", uiScrW() / 2, uiScrH() / 2);
}

// ---- 机上 WiFi 配置（扫描/列表/输密码）----

inline void uiDrawScanning() {
  auto& d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  d.setFont(&fonts::efontCN_16);
  d.setTextColor(TFT_CYAN, TFT_BLACK);
  d.drawString("扫描 WiFi …", uiScrW() / 2, uiScrH() / 2);
}

static const int UI_LIST_WIN = 5;

inline void uiDrawWifiList(int sel, const String& err, const std::vector<String>& aps) {
  auto& d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);

  d.setFont(&fonts::efontCN_14);
  d.setTextColor(TFT_CYAN, TFT_BLACK);
  d.drawString("选择 WiFi 网络", uiScrW() / 2, 12);

  int y = 28;
  if (err.length() > 0) {
    d.setFont(&fonts::efontCN_12);
    d.setTextColor(TFT_RED, TFT_BLACK);
    d.drawString(err, uiScrW() / 2, y);
    y += 16;
  }

  int total = (int)aps.size() + 1;  // 末项：手动输入
  int start = sel - UI_LIST_WIN / 2;
  if (start < 0) start = 0;
  if (start > total - UI_LIST_WIN) start = total - UI_LIST_WIN;
  if (start < 0) start = 0;

  d.setFont(&fonts::efontCN_12);
  for (int i = 0; i < UI_LIST_WIN; i++) {
    int idx = start + i;
    if (idx >= total) break;
    String label = (idx < (int)aps.size()) ? aps[idx] : "[手动输入网络]";
    if (label.length() > 18) label = label.substring(0, 17) + "…";
    if (idx == sel) {
      d.setTextColor(TFT_YELLOW, TFT_DARKGREY);
      d.drawString("> " + label, uiScrW() / 2, y);
    } else {
      d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      d.drawString("  " + label, uiScrW() / 2, y);
    }
    y += 16;
  }

  d.setFont(&fonts::efontCN_12);
  d.setTextColor(TFT_DARKGREY, TFT_BLACK);
  d.drawString(";.上下 ,/翻页 · 回车连接 · tab返回", uiScrW() / 2, uiScrH() - 8);
}

inline void uiDrawWifiInput(const String& title, const String& input,
                            bool masked, const String& err) {
  auto& d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);

  String t = title.length() ? title : "手动输入";
  if (t.length() > 14) t = t.substring(0, 13) + "…";
  d.setFont(&fonts::efontCN_14);
  d.setTextColor(TFT_CYAN, TFT_BLACK);
  d.drawString((masked ? "WiFi密码: " : "WiFi名称: ") + t,
               uiScrW() / 2, 14);

  d.drawRect(8, 32, uiScrW() - 16, 30, TFT_DARKGREY);
  d.setFont(&fonts::efontCN_14);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  String shown;
  if (masked) {
    for (unsigned i = 0; i < input.length(); i++) shown += '*';
  } else {
    shown = input;
  }
  shown += "_";
  d.setTextDatum(middle_left);
  d.drawString(shown, 14, 47);
  d.setTextDatum(middle_center);

  if (err.length() > 0) {
    d.setFont(&fonts::efontCN_12);
    d.setTextColor(TFT_RED, TFT_BLACK);
    d.drawString(err, uiScrW() / 2, 74);
  }

  d.setFont(&fonts::efontCN_12);
  d.setTextColor(TFT_DARKGREY, TFT_BLACK);
  d.drawString("回车确认 · tab返回 · del删除", uiScrW() / 2, uiScrH() - 8);
}
