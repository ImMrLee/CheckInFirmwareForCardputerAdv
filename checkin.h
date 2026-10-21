#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "config.h"
#include "profile.h"

inline void ntpBegin() {
  setenv("TZ", TZ_POSIX, 1);
  tzset();
  // 必须用 configTzTime：本核心的 configTime(0,0,...) 会调 setTimeZone(-0,0)
  // 把 TZ 覆盖成 UTC0DST0，导致设备跑 UTC+0（打卡日期整体错 8 小时）
  configTzTime(TZ_POSIX, NTP_SERVER_1, NTP_SERVER_2);
}

inline bool timeValid() {
  time_t now = time(nullptr);
  return now > 1704067200;  // 2024-01-01 之后才视为已同步
}

inline String formatNow(const char* fmt) {
  char buf[32];
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  strftime(buf, sizeof(buf), fmt, &t);
  return String(buf);
}

// 复刻 Android ageFromBirthday：周岁，生日当天不减（即当天计 +1）
inline int ageFromBirthday(const String& birthday) {
  if (birthday.length() != 10) return -1;
  int by = birthday.substring(0, 4).toInt();
  int bm = birthday.substring(5, 7).toInt();
  int bd = birthday.substring(8, 10).toInt();
  if (birthday[4] != '-' || birthday[7] != '-') return -1;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  int year = t.tm_year + 1900;
  int month = t.tm_mon + 1;
  int day = t.tm_mday;
  if (by > year || by < 1900 || bm < 1 || bm > 12 || bd < 1 || bd > 31) return -1;
  int age = year - by;
  if (month < bm || (month == bm && day < bd)) age--;
  if (age < 1) age = 1;
  if (age > 120) age = 120;
  return age;
}

// 复刻 Android isBirthdayToday：月日相同且生日年不晚于今年
inline bool isBirthdayToday(const String& birthday) {
  if (birthday.length() != 10) return false;
  String todayMd = formatNow("%m-%d");
  String birthMd = birthday.substring(5);
  int by = birthday.substring(0, 4).toInt();
  int nowY = 0;
  {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    nowY = t.tm_year + 1900;
  }
  return birthMd == todayMd && by > 0 && by <= nowY;
}

// 复刻 Android getGreeting 的时段问候
inline String greetingText() {
  if (isBirthdayToday(g_profile.birthday)) return "生日快乐";
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  int hour = t.tm_hour;
  if (hour >= 0 && hour <= 4)  return "夜深了";
  if (hour >= 5 && hour <= 8)  return "早上好";
  if (hour >= 9 && hour <= 11) return "上午好";
  if (hour >= 12 && hour <= 13) return "中午好";
  if (hour >= 14 && hour <= 17) return "下午好";
  if (hour >= 18 && hour <= 20) return "晚上好";
  return "晚安";
}

struct CheckinResult {
  bool ok;
  int code;
  String msg;
};

inline CheckinResult doCheckinPost() {
  CheckinResult r{false, 0, ""};

  String addr = g_profile.address;
  String city = addr.length() > 0 ? addr : "无定位";
  String address = addr;

  JsonDocument doc;
  doc["user_name"] = g_profile.name;
  doc["phone_number"] = g_profile.phone;
  int age = ageFromBirthday(g_profile.birthday);
  doc["age"] = (age > 0) ? age : 0;
  doc["gender"] = g_profile.gender;
  doc["emergency_contact"] = g_profile.emergency;
  doc["checkin_time"] = formatNow("%Y-%m-%d %H:%M:%S");
  doc["latitude"] = 0.0;
  doc["longitude"] = 0.0;
  doc["city"] = city;
  doc["address"] = address;

  String body;
  serializeJson(doc, body);

  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(SERVER_URL, SERVER_CERT)) {
    r.msg = "TLS 初始化失败";
    return r;
  }
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  r.code = code;
  if (code > 0) {
    r.ok = (code >= 200 && code < 300);
    r.msg = http.getString();
    if (r.msg.length() == 0) r.msg = r.ok ? "打卡成功" : "服务器无响应内容";
  } else {
    r.msg = http.errorToString(code);
    if (r.msg.length() == 0) r.msg = "网络错误";
  }
  http.end();
  return r;
}
