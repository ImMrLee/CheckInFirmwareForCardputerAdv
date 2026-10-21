#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include "config.h"

struct Profile {
  String ssid;
  String pass;
  String name;
  String phone;
  String birthday;
  String gender;
  String emergency;
  String address;
  String lastDate;
};

inline Profile g_profile;
inline Preferences g_prefs;
inline bool g_sdOk = false;

#define SD_DIR      "/CheckInInfo"
#define SD_WIFI     SD_DIR "/wifi.txt"
#define SD_PROFILE  SD_DIR "/profile.txt"
#define SD_WIFI_BK  SD_DIR "/wifi_bk.txt"

inline bool sdInit() {
  SPI.begin(40, 39, 14, 12);
  if (!SD.begin(12, SPI, 25000000)) {
    g_sdOk = false;
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    g_sdOk = false;
    return false;
  }
  g_sdOk = true;
  if (!SD.exists(SD_DIR)) SD.mkdir(SD_DIR);
  return true;
}

inline String sdReadFile(const char* path) {
  File f = SD.open(path, FILE_READ);
  if (!f || f.isDirectory()) return "";
  String s = f.readString();
  f.close();
  return s;
}

inline bool sdWriteFile(const char* path, const String& content) {
  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;
  size_t n = f.print(content);
  f.close();
  return n == content.length();
}

// 去掉行尾 \r 与首尾空白
inline String cleanLine(String s) {
  s.replace("\r", "");
  s.trim();
  return s;
}

inline void prefsPutAll() {
  g_prefs.putString("ssid", g_profile.ssid);
  g_prefs.putString("pass", g_profile.pass);
  g_prefs.putString("name", g_profile.name);
  g_prefs.putString("phone", g_profile.phone);
  g_prefs.putString("birthday", g_profile.birthday);
  g_prefs.putString("gender", g_profile.gender);
  g_prefs.putString("emergency", g_profile.emergency);
  g_prefs.putString("address", g_profile.address);
  g_prefs.putString("last_date", g_profile.lastDate);
}

inline void sdPutAll() {
  if (!g_sdOk) return;
  if (!SD.exists(SD_DIR)) SD.mkdir(SD_DIR);
  sdWriteFile(SD_WIFI, g_profile.ssid + "\n" + g_profile.pass + "\n");
  String body;
  body += "name=" + g_profile.name + "\n";
  body += "phone=" + g_profile.phone + "\n";
  body += "birthday=" + g_profile.birthday + "\n";
  body += "gender=" + g_profile.gender + "\n";
  body += "emergency=" + g_profile.emergency + "\n";
  body += "address=" + g_profile.address + "\n";
  body += "last_date=" + g_profile.lastDate + "\n";
  sdWriteFile(SD_PROFILE, body);
}

inline void profileSave() {
  prefsPutAll();
  sdPutAll();
}

inline void profileLoad() {
  bool prefsOk = g_prefs.begin("checkin", false);
  sdInit();

  // 1) 先以 NVS 为基准读入全部字段
  g_profile.ssid      = g_prefs.getString("ssid", "");
  g_profile.pass      = g_prefs.getString("pass", "");
  g_profile.name      = g_prefs.getString("name", "");
  g_profile.phone     = g_prefs.getString("phone", "");
  g_profile.birthday  = g_prefs.getString("birthday", "");
  g_profile.gender    = g_prefs.getString("gender", "");
  g_profile.emergency = g_prefs.getString("emergency", "");
  g_profile.address   = g_prefs.getString("address", "");
  g_profile.lastDate  = g_prefs.getString("last_date", "");
  String nvsLastDate  = g_profile.lastDate;

  // 2) SD 卡存在则逐字段覆盖，保证任一存储陈旧时都能被另一份兜底
  bool fromSd = false;
  if (g_sdOk && SD.exists(SD_PROFILE)) {
    String body = sdReadFile(SD_PROFILE);
    int pos = 0;
    while (pos < (int)body.length()) {
      int nl = body.indexOf('\n', pos);
      if (nl < 0) nl = body.length();
      String line = cleanLine(body.substring(pos, nl));
      pos = nl + 1;
      int eq = line.indexOf('=');
      if (eq <= 0) continue;
      String k = line.substring(0, eq);
      String v = line.substring(eq + 1);
      if (k == "name") g_profile.name = v;
      else if (k == "phone") g_profile.phone = v;
      else if (k == "birthday") g_profile.birthday = v;
      else if (k == "gender") g_profile.gender = v;
      else if (k == "emergency") g_profile.emergency = v;
      else if (k == "address") g_profile.address = v;
      else if (k == "last_date") g_profile.lastDate = v;
    }
    fromSd = true;
  }
  if (g_sdOk && SD.exists(SD_WIFI)) {
    String body = sdReadFile(SD_WIFI);
    int nl = body.indexOf('\n');
    if (nl >= 0) {
      String s = cleanLine(body.substring(0, nl));
      if (s.length() > 0) {  // 只在有效时覆盖，避免空文件清掉 NVS 凭据
        g_profile.ssid = s;
        g_profile.pass = cleanLine(body.substring(nl + 1));
        fromSd = true;
      }
    }
  }

  // 3) 打卡状态取两边较新的日期（YYYY-MM-DD 字典序即时间序），
  //    防止 SD/NVS 任一份陈旧导致“重启后打卡状态被重置”
  bool lastDateFixed = false;
  if (nvsLastDate > g_profile.lastDate) {
    g_profile.lastDate = nvsLastDate;
    lastDateFixed = true;
  }

  if (fromSd) prefsPutAll();      // SD→NVS 同步，双保险
  if (lastDateFixed) sdPutAll();  // NVS 的打卡日期更新：回写 SD，保证两份一致

  Serial.printf("[profile] prefs=%s sd=%s 来源=%s lastDate=%s(NVS=%s) ssid=%s name=%s\n",
                prefsOk ? "ok" : "FAIL", g_sdOk ? "ok" : "无",
                fromSd ? "SD卡" : "NVS",
                g_profile.lastDate.c_str(), nvsLastDate.c_str(),
                g_profile.ssid.c_str(), g_profile.name.c_str());
}

inline bool hasWifiCreds() {
  return g_profile.ssid.length() > 0 || String(DEFAULT_WIFI_SSID).length() > 0;
}

// 生效的 WiFi：SD/NVS 优先，否则用编译期默认
inline String effectiveSsid() {
  return g_profile.ssid.length() > 0 ? g_profile.ssid : String(DEFAULT_WIFI_SSID);
}
inline String effectivePass() {
  return g_profile.ssid.length() > 0 ? g_profile.pass : String(DEFAULT_WIFI_PASS);
}

// 覆盖 WiFi 前把当前凭据备份，连不上时可自动回退
inline void backupWifiCreds() {
  String cur = effectiveSsid();
  String curp = effectivePass();
  if (cur.length() == 0) return;
  g_prefs.putString("bk_ssid", cur);
  g_prefs.putString("bk_pass", curp);
  if (g_sdOk) sdWriteFile(SD_WIFI_BK, cur + "\n" + curp + "\n");
}

inline String backupSsid() {
  if (g_sdOk && SD.exists(SD_WIFI_BK)) {
    String body = sdReadFile(SD_WIFI_BK);
    int nl = body.indexOf('\n');
    if (nl >= 0) {
      String s = cleanLine(body.substring(0, nl));
      if (s.length() > 0) return s;
    }
  }
  return g_prefs.getString("bk_ssid", "");
}

inline String backupPass() {
  if (g_sdOk && SD.exists(SD_WIFI_BK)) {
    String body = sdReadFile(SD_WIFI_BK);
    int nl = body.indexOf('\n');
    if (nl >= 0) return cleanLine(body.substring(nl + 1));
  }
  return g_prefs.getString("bk_pass", "");
}

inline bool profileComplete() {
  return g_profile.name.length() > 0 &&
         g_profile.phone.length() > 0 &&
         g_profile.birthday.length() > 0 &&
         g_profile.gender.length() > 0;
}
