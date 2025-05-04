#ifndef GFXFF
#define GFXFF 1
#endif

#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "doto-regular18pt7b.h"
#include <time.h>
#include "include/config.h"

WiFiMulti wifiMulti;
TFT_eSPI tft;

struct VacdmSlotInfo {
  String tobt;
  String tsat;
  String ctot;
  String sid;
  String runway;
  bool hasRunway;
};

struct VacdmServer {
  const char* baseUrl;
  bool scandinavianFormat;
};

const VacdmServer vacdm_servers[] = {
  {"https://app.vacdm.net/api/v1/pilots", true},
  {"https://vacdm.vatita.net/api/v1/pilots", true},
  {"https://cdm.vatsim-scandinavia.org/api/v1/pilots", true},
  {"https://cdm.vatsim.fr/api/v1/pilots", true},
  {"https://vacdm.vatprc.net/api/v1/pilots", true},
  {"https://vacdm.vacc-austria.org/api/v1/pilots", true},
  {"https://cdm-server-production.up.railway.app/slotService/callsign", false}
};
const size_t vacdm_server_count = sizeof(vacdm_servers) / sizeof(vacdm_servers[0]);

String getCallsignFromCid(const String& cid);
VacdmSlotInfo getVacdmData(const String& callsign);
void displayData(const String& callsign, const VacdmSlotInfo& slot);
void connectToWiFi();
String formatTimeShort(const String& t);
time_t parseIsoUtcTime(const String& in);
bool isAircraftAirborne(const String& cid);

unsigned long lastUpdate = 0;
int offlineCount = 0;
const int offlineThreshold = 3;
const unsigned long refreshInterval = 30000;
String cid = VATSIM_CID;

void setup() {
  pinMode(21, OUTPUT);
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);
  configTime(0, 0, "pool.ntp.org");

  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(&doto_regular18pt7b);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString("VDGS Display", 160, 40, 1);
  tft.drawCentreString("by PLVACC",    160, 75, 1);

  connectToWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    tft.println("No WiFi available");
    return;
  }

  delay(1000);
  // get callsign and ensure it's available
  String callsign = getCallsignFromCid(cid);
  if (callsign.isEmpty()) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawCentreString("Waiting for", 160, 60, 1);
    tft.drawCentreString("login...",     160, 90, 1);
    return;
  }
  // Check airborne immediately after obtaining callsign
  if (isAircraftAirborne(cid)) {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(20, 60);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.println("DEPARTED — vACDM OFF");
    return;
  }

  VacdmSlotInfo slot = getVacdmData(callsign);
  displayData(callsign, slot);
}

void loop() {
  if (millis() - lastUpdate > refreshInterval) {
    Serial.println("[MAIN] Odświeżanie danych...");
    String callsign = getCallsignFromCid(cid);
    if (callsign.isEmpty()) {
      offlineCount++;
      Serial.printf("[MAIN] CID offline (attempt %d)\n", offlineCount);
      tft.fillScreen(TFT_BLACK);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      if (offlineCount >= offlineThreshold) {
        tft.drawCentreString("Waiting for", 160, 60, 1);
        tft.drawCentreString("login...",     160, 90, 1);
      } else {
        tft.drawCentreString("User NOT",   160, 60, 1);
        tft.drawCentreString("logged IN!", 160, 90, 1);
      }
      lastUpdate = millis();
      return;
    }
    offlineCount = 0;

    VacdmSlotInfo slot = getVacdmData(callsign);
    displayData(callsign, slot);
    lastUpdate = millis();
  }
}

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID_1, WIFI_PASSWORD_1);
  Serial.print("Connecting to network: "); Serial.println(WIFI_SSID_1);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500); Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nNetwork 1 connected");
    return;
  }
  WiFi.begin(WIFI_SSID_2, WIFI_PASSWORD_2);
  Serial.print("\nConnecting to network: "); Serial.println(WIFI_SSID_2);
  start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500); Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED)
    Serial.println("\nNetwork 2 connected");
  else
    Serial.println("\nUnable to connect to WiFi");
}

String getCallsignFromCid(const String& cid) {
  HTTPClient http;
  String url = "https://api.vatsim.net/v2/members/" + cid + "/status";
  http.begin(url);
  int code = http.GET();
  if (code != 200) {
    http.end();
    return "";
  }
  String payload = http.getString();
  http.end();

  StaticJsonDocument<4096> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[VATSIM v2] JSON error: %s\n", err.c_str());
    return "";
  }
  return doc["callsign"].is<String>() ? doc["callsign"].as<String>() : String();
}

VacdmSlotInfo getVacdmData(const String& callsign) {
  VacdmSlotInfo slot;
  for (size_t i = 0; i < vacdm_server_count; ++i) {
    const auto& server = vacdm_servers[i];
    String url;
    if (server.scandinavianFormat) {
      url = String(server.baseUrl);
      url += "/";
      url += callsign;
    } else {
      url = String(server.baseUrl);
      url += "?callsign=";
      url += callsign;
    }

    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode != 200) {
      http.end();
      continue;
    }
    String payload = http.getString();
    http.end();

    StaticJsonDocument<16384> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) continue;

    if (server.scandinavianFormat && doc.is<JsonArray>()) {
      for (JsonObject item : doc.as<JsonArray>()) {
        if (item["callsign"].as<String>() == callsign) {
          slot.tobt      = item["vacdm"]["tobt"].as<String>();
          slot.tsat      = item["vacdm"]["tsat"].as<String>();
          slot.sid       = item["clearance"]["sid"].as<String>();
          if (item["clearance"].containsKey("dep_rwy")) {
            slot.runway    = item["clearance"]["dep_rwy"].as<String>();
            slot.hasRunway = true;
          }
          return slot;
        }
      }
    } else if (!server.scandinavianFormat && doc.is<JsonObject>()) {
      JsonObject obj = doc.as<JsonObject>();
      slot.tobt = obj["tobt"].as<String>();
      slot.tsat = obj["tsat"].as<String>();
      slot.sid  = obj["sid"].as<String>();
      if (obj["cdmData"]["ctot"].is<String>()) {
        slot.ctot = obj["cdmData"]["ctot"].as<String>();
      }
      slot.hasRunway = false;
      return slot;
    }
  }
  return slot;
}

String formatTimeShort(const String& t) {
  if (t.length() < 4) return "--:--";
  return t.substring(0,2) + ":" + t.substring(2,4) + "Z";
}

time_t parseIsoUtcTime(const String& in) {
  struct tm tm = {};
  time_t now = time(nullptr);
  gmtime_r(&now, &tm);
  if (in.length() == 4) {
    tm.tm_hour = in.substring(0,2).toInt();
    tm.tm_min  = in.substring(2,4).toInt();
  } else if (in.length() >= 16) {
    tm.tm_year = in.substring(0,4).toInt() - 1900;
    tm.tm_mon  = in.substring(5,7).toInt() - 1;
    tm.tm_mday = in.substring(8,10).toInt();
    tm.tm_hour = in.substring(11,13).toInt();
    tm.tm_min  = in.substring(14,16).toInt();
  }
  tm.tm_sec   = 0;
  tm.tm_isdst = 0;
  return mktime(&tm);
}

bool isAircraftAirborne(const String& cid) {
  if (SKIP_AIRBORNE_CHECK) return false;
  HTTPClient http;
  http.begin("https://data.vatsim.net/v3/vatsim-data.json");
  int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }
  auto& stream = http.getStream();

  StaticJsonDocument<256> filter;
  filter["pilots"][0]["cid"] = true;
  filter["pilots"][0]["altitude"] = true;
  filter["pilots"][0]["groundspeed"] = true;

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, stream, DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("[AIRBORNE CHECK] parse error: %s\n", err.c_str());
    return false;
  }

  for (JsonObject p : doc["pilots"].as<JsonArray>()) {
    if (p["cid"].as<String>() == cid) {
      int alt = p["altitude"] | 0;
      int gs  = p["groundspeed"] | 0;
      Serial.printf("[AIRBORNE CHECK] ALT: %d ft, GS: %d kt\n", alt, gs);
      return alt > 1000 && gs > 80;
    }
  }
  return false;
}

void displayData(const String& callsign, const VacdmSlotInfo& slot) {
  uint16_t ledOrange = tft.color565(229,135,55);
  if (isAircraftAirborne(cid)) {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(20, 60);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.println("DEPARTED — vACDM OFF");
    return;
  }
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(ledOrange, TFT_BLACK);
  tft.setFreeFont(&doto_regular18pt7b);

  String tobt = formatTimeShort(slot.tobt);
  String tsat = formatTimeShort(slot.tsat);
  String ctot = formatTimeShort(slot.ctot);
  int x = 160, y = 10, sp = 30;

  tft.drawCentreString(callsign, x, y, 1); y += sp;
  tft.drawCentreString("TOBT " + tobt, x, y, 1); y += sp;
  tft.drawCentreString("TSAT " + tsat, x, y, 1); y += sp;
  tft.drawCentreString("CTOT " + ctot, x, y, 1); y += sp;

  int diff = int((time(nullptr) - parseIsoUtcTime(slot.tsat)) / 60);
  String ds = (diff > 0 ? "+" : "") + String(diff);
  tft.drawCentreString(ds, x, y, 1); y += sp;

  if (slot.hasRunway) {
    tft.drawCentreString("PLANNED RWY " + slot.runway, x, y, 1); y += sp;
  }
  tft.drawCentreString("SID " + slot.sid, x, y, 1);
  Serial.printf("[vACDM] %s | TOBT %s | TSAT %s | CTOT %s | SID %s\n",
                callsign.c_str(), tobt.c_str(), tsat.c_str(), ctot.c_str(), slot.sid.c_str());
}
