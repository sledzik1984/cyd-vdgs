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

// WiFi and display objects
WiFiMulti wifiMulti;
TFT_eSPI tft;

// Slot info structure
struct VacdmSlotInfo {
  String tobt;
  String tsat;
  String ctot;
  String sid;
  String runway;
  bool hasRunway;
};

// ACDM servers
struct VacdmServer { const char* baseUrl; bool scandinavianFormat; };
const VacdmServer vacdm_servers[] = {
  {"https://app.vacdm.net/api/v1/pilots", true},
  {"https://vacdm.vatita.net/api/v1/pilots", true},
  {"https://cdm.vatsim-scandinavia.org/api/v1/pilots", true},
  {"https://cdm.vatsim.fr/api/v1/pilots", true},
  {"https://vacdm.vatprc.net/api/v1/pilots", true},
  {"https://vacdm.vacc-austria.org/api/v1/pilots", true},
  {"https://cdm-server-production.up.railway.app/slotService/callsign", false}
};
const size_t vacdm_server_count = sizeof(vacdm_servers)/sizeof(vacdm_servers[0]);

// Prototypes
void connectToWiFi();
String getCallsignFromCid(const String& cid);
VacdmSlotInfo getVacdmData(const String& callsign);
bool isAircraftAirborne(const String& cid);
String formatTimeShort(const String& t);
time_t parseIsoUtcTime(const String& in);
void displayData(const String& callsign, const VacdmSlotInfo& slot);

// Globals
const String cid = VATSIM_CID;
unsigned long lastUpdate = 0;
int offlineCount = 0;
const int offlineThreshold = 3;
const unsigned long refreshInterval = 30000;

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
  Serial.print("Connected to SSID: "); Serial.println(WiFi.SSID());

  delay(500);
  String callsign = getCallsignFromCid(cid);
  if (callsign.isEmpty()) {
    tft.drawCentreString("Waiting for login...", 160, 120, 1);
    return;
  }

  // Check airborne immediately
  if (isAircraftAirborne(cid)) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawCentreString("DEPARTED", 160, 100, 1);
    tft.drawCentreString("vACDM OFF", 160, 130, 1);
    return;
  }

  VacdmSlotInfo slot = getVacdmData(callsign);
  displayData(callsign, slot);
  lastUpdate = millis();
}

void loop() {
  if (millis() - lastUpdate < refreshInterval) return;

  Serial.println("[MAIN] Refreshing data...");
  String callsign = getCallsignFromCid(cid);
  if (callsign.isEmpty()) {
    offlineCount++;
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    if (offlineCount >= offlineThreshold)
      tft.drawCentreString("Waiting for login...", 160, 120, 1);
    else
      tft.drawCentreString("User NOT logged IN!", 160, 120, 1);
    lastUpdate = millis();
    return;
  }
  offlineCount = 0;

  if (isAircraftAirborne(cid)) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawCentreString("DEPARTED", 160, 100, 1);
    tft.drawCentreString("vACDM OFF", 160, 130, 1);
    lastUpdate = millis();
    return;
  }

  VacdmSlotInfo slot = getVacdmData(callsign);
  displayData(callsign, slot);
  lastUpdate = millis();
}

void connectToWiFi() {
  wifiMulti.addAP(WIFI_SSID_1, WIFI_PASSWORD_1);
  wifiMulti.addAP(WIFI_SSID_2, WIFI_PASSWORD_2);
  Serial.print("Connecting to WiFi");
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
  if (code != 200) { http.end(); return ""; }
  String payload = http.getString();
  http.end();

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, payload)) return "";
  return doc["callsign"].as<String>();
}

VacdmSlotInfo getVacdmData(const String& callsign) {
  VacdmSlotInfo slot;
  for (auto& srv : vacdm_servers) {
    String url = String(srv.baseUrl);
    if (srv.scandinavianFormat) url += "/" + callsign;
    else                       url += "?callsign=" + callsign;

    HTTPClient http;
    http.begin(url);
    if (http.GET() == 200) {
      String payload = http.getString();
      StaticJsonDocument<2048> doc;
      if (!deserializeJson(doc, payload)) {
        if (srv.scandinavianFormat) {
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
          if (obj["cdmData"]["ctot"].is<String>())
            slot.ctot = obj["cdmData"]["ctot"].as<String>();
          return slot;
        }
      }
    }
    http.end();
  }
  return slot;
}

bool isAircraftAirborne(const String& cid) {
  if (SKIP_AIRBORNE_CHECK) return false;
  HTTPClient http;
  http.begin("https://data.vatsim.net/v3/vatsim-data.json");
  if (http.GET() != 200) {
    Serial.println("[AIR] HTTP error");
    http.end();
    return false;
  }
  WiFiClient& s = http.getStream();

  // Scan stream for "cid":<cid>
  String marker = String("\"cid\":") + cid;
  if (s.find(marker.c_str())) {
    if (s.find("\"altitude\":")) {
      int alt = s.parseInt();
      if (s.find("\"groundspeed\":")) {
        int gs = s.parseInt();
        Serial.printf("[AIR] cid=%s alt=%d gs=%d\n", cid.c_str(), alt, gs);
        http.end();
        return alt>1000 && gs>80;
      }
    }
  }

  http.end();
  Serial.println("[AIR] no data");
  return false;
}

String formatTimeShort(const String& t) {
  if (t.length()<4) return "--:--";
  return t.substring(0,2)+":"+t.substring(2,4)+"Z";
}

time_t parseIsoUtcTime(const String& in) {
  struct tm tm{};
  time_t now = time(nullptr);
  gmtime_r(&now,&tm);
  if (in.length()==4) {
    tm.tm_hour = in.substring(0,2).toInt();
    tm.tm_min  = in.substring(2,4).toInt();
  }
  tm.tm_sec=0;tm.tm_isdst=0;
  return mktime(&tm);
}

void displayData(const String& callsign, const VacdmSlotInfo& slot) {
  uint16_t clr = tft.color565(229,135,55);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(clr, TFT_BLACK);
  tft.setFreeFont(&doto_regular18pt7b);
  int x=160,y=10,sp=30;
  tft.drawCentreString(callsign,x,y,1);y+=sp;
  tft.drawCentreString("TOBT "+formatTimeShort(slot.tobt),x,y,1);y+=sp;
  tft.drawCentreString("TSAT "+formatTimeShort(slot.tsat),x,y,1);y+=sp;
  tft.drawCentreString("CTOT "+formatTimeShort(slot.ctot),x,y,1);y+=sp;
  int diff=(time(nullptr)-parseIsoUtcTime(slot.tsat))/60;
  tft.drawCentreString((diff>0?"+":"")+String(diff),x,y,1);y+=sp;
  if(slot.hasRunway){tft.drawCentreString("RWY "+slot.runway,x,y,1);y+=sp;}
  tft.drawCentreString("SID "+slot.sid,x,y,1);
}
