#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <vector>
#include "config.h"
#include "profile.h"

inline WebServer g_server(80);
inline String g_scanOptions;
inline bool g_scanned = false;
inline String g_usedSsid;
inline bool g_usingBackup = false;
inline bool g_servicesStarted = false;
inline bool g_mdnsStarted = false;
inline bool g_webHandlersRegistered = false;

static const char PORTAL_HTML_HEAD[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="zh-CN"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>打卡助手配置</title>
<style>
body{font-family:sans-serif;background:#f0f4f8;margin:0;padding:16px}
.c{max-width:420px;margin:0 auto;background:#fff;border-radius:12px;padding:20px;box-shadow:0 2px 8px rgba(0,0,0,.08)}
h1{font-size:20px;margin:0 0 4px;color:#1a5fb4}
.meta{font-size:12px;color:#888;margin-bottom:14px}
label{display:block;font-size:14px;font-weight:600;margin:12px 0 4px;color:#333}
input,select{width:100%;box-sizing:border-box;padding:10px;font-size:16px;border:1px solid #ccc;border-radius:8px}
button{width:100%;margin-top:18px;padding:12px;font-size:17px;font-weight:700;color:#fff;background:#1a5fb4;border:0;border-radius:8px}
.hint{font-size:12px;color:#999;margin-top:2px}
</style></head><body><div class="c">
<h1>打卡助手 · 配置</h1>
<div class="meta">固件 v__FW__ · 已联网 __IP__ · 存储 __SD__</div>
<form method="POST" action="/save">
<label>WiFi 名称（留空则不改）</label>
<input name="wifi_ssid" list="ssidlist" value="" placeholder="__SSIDPH__">
<datalist id="ssidlist">__SCAN__</datalist>
<label>WiFi 密码（改 WiFi 时必填）</label>
<input name="wifi_pass" type="password" placeholder="不改密码请留空">
<label>姓名（2-4 个汉字）</label>
<input name="user_name" value="__NAME__" required>
<label>手机号</label>
<input name="phone" type="tel" maxlength="11" value="__PHONE__" required>
<label>生日</label>
<input name="birthday" type="date" value="__BDAY__" required>
<label>性别</label>
<select name="gender">
<option value="男" __GM__>男</option>
<option value="女" __GF__>女</option>
</select>
<label>紧急联系人手机号（选填）</label>
<input name="emergency" type="tel" maxlength="11" value="__EMG__">
<label>家庭地址（选填，无定位时上报）</label>
<input name="address" value="__ADDR__">
<button type="submit">保存并重启</button>
</form></div></body></html>
)HTML";

static const char PORTAL_OK_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>已保存</title>
<style>body{font-family:sans-serif;display:flex;align-items:center;justify-content:center;height:95vh;background:#e8f5e9}
div{text-align:center;color:#2e7d32}h1{font-size:24px}</style></head>
<body><div><h1>已保存</h1><p>设备重启中，请稍候……</p></div></body></html>
)HTML";

inline String htmlEscape(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

inline void handleRoot() {
  if (!g_scanned) {
    int n = WiFi.scanNetworks();
    g_scanOptions = "";
    for (int i = 0; i < n && i < 20; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;
      g_scanOptions += "<option value=\"" + htmlEscape(ssid) + "\">";
    }
    WiFi.scanDelete();
    g_scanned = true;
  }
  String page = FPSTR(PORTAL_HTML_HEAD);
  page.replace("__FW__", FW_VERSION);
  page.replace("__IP__", WiFi.localIP().toString());
  page.replace("__SD__", g_sdOk ? "SD卡✓" : "无SD卡(仅NVS)");
  page.replace("__SCAN__", g_scanOptions);
  page.replace("__SSIDPH__", g_usedSsid);
  page.replace("__NAME__", htmlEscape(g_profile.name));
  page.replace("__PHONE__", htmlEscape(g_profile.phone));
  page.replace("__BDAY__", htmlEscape(g_profile.birthday));
  page.replace("__GM__", g_profile.gender == "男" ? "selected" : "");
  page.replace("__GF__", g_profile.gender == "女" ? "selected" : "");
  page.replace("__EMG__", htmlEscape(g_profile.emergency));
  page.replace("__ADDR__", htmlEscape(g_profile.address));
  g_server.send(200, "text/html; charset=utf-8", page);
}

inline void handleSave() {
  if (g_server.hasArg("wifi_ssid")) {
    String ssid = g_server.arg("wifi_ssid");
    ssid.trim();
    if (ssid.length() > 0 && ssid != g_usedSsid) {
      backupWifiCreds();
      g_profile.ssid = ssid;
      if (g_server.hasArg("wifi_pass")) g_profile.pass = g_server.arg("wifi_pass");
    }
  }
  if (g_server.hasArg("user_name")) g_profile.name = g_server.arg("user_name");
  if (g_server.hasArg("phone")) g_profile.phone = g_server.arg("phone");
  if (g_server.hasArg("birthday")) g_profile.birthday = g_server.arg("birthday");
  if (g_server.hasArg("gender")) g_profile.gender = g_server.arg("gender");
  if (g_server.hasArg("emergency")) g_profile.emergency = g_server.arg("emergency");
  if (g_server.hasArg("address")) g_profile.address = g_server.arg("address");
  profileSave();
  g_server.send(200, "text/html; charset=utf-8", FPSTR(PORTAL_OK_HTML));
  delay(500);
  ESP.restart();
}

inline void handleNotFound() {
  g_server.send(404, "text/plain; charset=utf-8", "Not Found");
}

inline void startStaServices() {
  // 幂等：处理器只注册一次；mDNS/服务器在 WiFi 每次恢复连接后重新拉起
  if (!g_webHandlersRegistered) {
    g_server.on("/", HTTP_GET, handleRoot);
    g_server.on("/save", HTTP_POST, handleSave);
    g_server.onNotFound(handleNotFound);
    g_webHandlersRegistered = true;
  }
  if (g_servicesStarted) g_server.stop();
  if (g_mdnsStarted) {
    MDNS.end();
    g_mdnsStarted = false;
  }
  g_mdnsStarted = MDNS.begin(MDNS_HOSTNAME);
  if (g_mdnsStarted) MDNS.addService("http", "tcp", 80);
  g_server.begin();
  g_servicesStarted = true;
}

// 打卡成功后关闭 WiFi 省电；之后打卡或按 w 配网时会自动恢复
inline void wifiOffForPowerSave() {
  WiFi.disconnect(true);  // 断开并关闭射频
  g_servicesStarted = false;
  Serial.println("[net] WiFi 已关闭（打卡后省电模式）");
}

inline bool tryConnect(const String& ssid, const String& pass) {
  if (ssid.length() == 0) return false;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(MDNS_HOSTNAME);
  Serial.printf("[net] 连接 WiFi: %s\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    g_usedSsid = ssid;
    Serial.printf("[net] 已连接: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  return false;
}

// 主凭据（NVS/编译默认）失败时自动回退到上一份可用凭据
inline bool wifiConnectSta() {
  g_usingBackup = false;
  if (tryConnect(effectiveSsid(), effectivePass())) return true;
  String bs = backupSsid();
  if (bs.length() > 0 && bs != effectiveSsid()) {
    Serial.println("[net] 主凭据失败，回退备份凭据");
    if (tryConnect(bs, backupPass())) {
      g_usingBackup = true;
      return true;
    }
  }
  Serial.println("[net] 所有凭据均连接失败");
  return false;
}

// 扫描周边 AP，按信号强度排序、去重
inline std::vector<String> scanApList() {
  std::vector<String> out;
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n && (int)out.size() < 30; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;
    bool dup = false;
    for (auto& x : out) {
      if (x == s) { dup = true; break; }
    }
    if (!dup) out.push_back(s);
  }
  WiFi.scanDelete();
  return out;
}

inline void netLoop() {
  g_server.handleClient();
}
