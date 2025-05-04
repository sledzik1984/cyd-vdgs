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

// Function declarations
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
    tft.drawCentreString("No WiFi available", 160, 120, 1);
    return;
  }

  delay(1000);
  String callsign = getCallsignFromCid(cid);
  if (callsign.isEmpty()) {
    tft.drawCentreString("Waiting for login...", 160, 120, 1);
    return;
  }

  // Initial check for airborne
  if (isAircraftAirborne(cid)) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawCentreString("DEPARTED — vACDM OFF", 160, 120, 1);
    return;
  }

  VacdmSlotInfo slot = getVacdmData(callsign);
  displayData(callsign, slot);
  lastUpdate = millis();
}

void loop() {
  if (millis() - lastUpdate > refreshInterval) {
    Serial.println("[MAIN] Refreshing data...");
    String callsign = getCallsignFromCid(cid);
    if (callsign.isEmpty()) {
      offlineCount++;
      tft.fillScreen(TFT_BLACK);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      if (offlineCount >= offlineThreshold) {
        tft.drawCentreString("Waiting for login...", 160, 120, 1);
      } else {
        tft.drawCentreString("User NOT logged IN!", 160, 120, 1);
      }
      lastUpdate = millis();
      return;
    }
    offlineCount = 0;

    if (isAircraftAirborne(cid)) {
      tft.fillScreen(TFT_BLACK);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawCentreString("DEPARTED — vACDM OFF", 160, 120, 1);
      lastUpdate = millis();
      return;
    }

    VacdmSlotInfo slot = getVacdmData(callsign);
    displayData(callsign, slot);
    lastUpdate = millis();
  }
}

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI_SSID_1, WIFI_PASSWORD_1);
  wifiMulti.addAP(WIFI_SSID_2, WIFI_PASSWORD_2);
  Serial.print("Connecting to WiFi...");
  while (wifiMulti.run() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.println(" connected");
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
  auto err = deserializeJson(doc, payload);
  if (err) {
    Serial.println("[VATSIM v2] JSON parse error");
    return "";
  }
  return doc["callsign"].as<String>();
}

VacdmSlotInfo getVacdmData(const String& callsign) {
  VacdmSlotInfo slot;
  for (size_t i = 0; i < vacdm_server_count; ++i) {
    const auto& server = vacdm_servers[i];
    String url = String(server.baseUrl);
    if (server.scandinavianFormat) url += "/" + callsign;
    else url += "?callsign=" + callsign;

    HTTPClient http;
    http.begin(url);
    int code = http.GET();
    if (code != 200) { http.end(); continue; }
    String payload = http.getString();
    http.end();

    StaticJsonDocument<16384> doc;
    auto err = deserializeJson(doc, payload);
    if (err) continue;

    if (server.scandinavianFormat) {
      for (JsonObject item : doc.as<JsonArray>()) {
        if (item["callsign"] == callsign) {
          slot.tobt = item["vacdm"]["tobt"].as<String>();
          slot.tsat = item["vacdm"]["tsat"].as<String>();
          slot.sid  = item["clearance"]["sid"].as<String>();
          if (item["clearance"].containsKey("dep_rwy")) {
            slot.runway = item["clearance"]["dep_rwy"].as<String>();
            slot.hasRunway = true;
          }
          return slot;
        }
      }
    } else {
      JsonObject obj = doc.as<JsonObject>();
      slot.tobt = obj["tobt"].as<String>();
      slot.tsat = obj["tsat"].as<String>();
      slot.sid  = obj["sid"].as<String>();
      if (obj["cdmData"]["ctot"].is<String>()) slot.ctot = obj["cdmData"]["ctot"].as<String>();
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
    tm.tm_year = in.substring(0,4).toInt()-1900;
    tm.tm_mon  = in.substring(5,7).toInt()-1;
    tm.tm_mday = in.substring(8,10).toInt();
    tm.tm_hour = in.substring(11,13).toInt();
    tm.tm_min  = in.substring(14,16).toInt();
  }
  tm.tm_sec = 0; tm.tm_isdst = 0;
  return mktime(&tm);
}

bool isAircraftAirborne(const String& cid) {
  if (SKIP_AIRBORNE_CHECK) return false;
  HTTPClient http;
  http.begin("https://data.vatsim.net/v3/vatsim-data.json");
  int code = http.GET();
  if (code != 200) {
    http.end();
    Serial.printf("[AIRBORNE CHECK] HTTP error: %d\n", code);
    return false;
  }
  auto& stream = http.getStream();

  StaticJsonDocument<256> filter;
  filter["pilots"][0]["cid"] = true;
  filter["pilots"][0]["altitude"] = true;
  filter["pilots"][0]["groundspeed"] = true;

  StaticJsonDocument<512> doc;
  auto err = deserializeJson(doc, stream, DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("[AIRBORNE CHECK] parse error: %s\n", err.c_str());
    return false;
  }

  // Debug: print filtered doc
  Serial.println("[AIRBORNE CHECK] Filtered JSON:");
  serializeJsonPretty(doc, Serial);
  Serial.println("[AIRBORNE CHECK] End of filtered JSON");

  for (JsonObject p : doc["pilots"].as<JsonArray>()) {
    long entryCid = p["cid"].as<long>();
    int alt = p["altitude"] | 0;
    int gs  = p["groundspeed"] | 0;
    Serial.printf("[AIRBORNE CHECK] Entry -> cid: %ld, alt: %d, gs: %d\n", entryCid, alt, gs);
    if (entryCid == cid.toInt()) {
      Serial.println("[AIRBORNE CHECK] Matching CID found");
      bool airborne = alt > 1000 && gs > 80;
      Serial.printf("[AIRBORNE CHECK] Result: %s\n", airborne ? "true" : "false");
      return airborne;
    }
  }
  Serial.println("[AIRBORNE CHECK] CID not found");
  return false;
}

void displayData(const String& callsign, const VacdmSlotInfo& slot) {
  uint16_t ledOrange = tft.color565(229,135,55);
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

  int diff = (int)((time(nullptr) - parseIsoUtcTime(slot.tsat)) / 60);
  String ds = (diff>0?"+":"") + String(diff);
  tft.drawCentreString(ds, x, y, 1); y += sp;

  if (slot.hasRunway) {
    tft.drawCentreString("PLANNED RWY " + slot.runway, x, y, 1); y += sp;
  }
  tft.drawCentreString("SID " + slot.sid, x, y, 1);
  Serial.printf("[vACDM] %s | TOBT %s | TSAT %s | CTOT %s | SID %s\n",
                callsign.c_str(), tobt.c_str(), tsat.c_str(), ctot.c_str(), slot.sid.c_str());
}
