# 1 "C:\\Users\\Piotrek\\AppData\\Local\\Temp\\tmp3gqdguz_"
#include <Arduino.h>
# 1 "C:/Users/Piotrek/Downloads/cyd-vgds/cyd-vgds.ino"
#ifndef GFXFF
#define GFXFF 1
#endif

#include <TFT_eSPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "doto-regular18pt7b.h"
#include <time.h>
#include "include/config.h"


TFT_eSPI tft = TFT_eSPI();

struct VacdmSlotInfo {
  String tobt = "";
  String tsat = "";
  String sid = "";
  String runway = "";
  bool hasRunway = false;
};


const String vatsim_data_url = "https://data.vatsim.net/v3/vatsim-data.json";
struct VacdmServer {
  String baseUrl;
  bool scandinavianFormat;
};

const VacdmServer vacdm_servers[] = {
  { "https://app.vacdm.net/api/v1/pilots", true },
  { "https://vacdm.vatita.net/api/v1/pilots", true },
  { "https://cdm.vatsim-scandinavia.org/api/v1/pilots", true },
  { "https://cdm.vatsim.fr/api/v1/pilots", true },
  { "https://vacdm.vatprc.net/api/v1/pilots", true },
  { "https://vacdm.vacc-austria.org/api/v1/pilots", true },
  { "https://cdm-server-production.up.railway.app/slotService/callsign", false }
};

const size_t vacdm_server_count = sizeof(vacdm_servers) / sizeof(vacdm_servers[0]);


String getCallsignFromCid(String cid);

void displayData(const String& callsign, const String& dataJson);

unsigned long lastUpdate = 0;
const unsigned long refreshInterval = 30000;

String cid = VATSIM_CID;
void setup();
void loop();
VacdmSlotInfo getVacdmData(String callsign);
String formatTimeShort(const String& timeStr);
time_t parseIsoUtcTime(const String& input);
bool isAircraftAirborne(String cid);
void displayData(const String& callsign, const VacdmSlotInfo& slot);
#line 53 "C:/Users/Piotrek/Downloads/cyd-vgds/cyd-vgds.ino"
void setup() {
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);

  configTime(0, 0, "pool.ntp.org");


  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);



  tft.setFreeFont(&doto_regular18pt7b);


  tft.setCursor(20, 20);
  tft.println("VDGS Display");
  tft.println("by PLVACC");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    tft.print(".");
    Serial.print(".");
  }
  Serial.println("WiFi connected");
  tft.println("WiFi OK");

  delay(1000);

  String callsign = getCallsignFromCid(cid);
  if (callsign == "") {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(10, 50);
    tft.println("NO CALLSIGN");
    return;
  }

  VacdmSlotInfo vacdm_slot = getVacdmData(callsign);
  displayData(callsign, vacdm_slot);
}

void loop() {
  if (millis() - lastUpdate > refreshInterval) {
    Serial.println("[MAIN] Odświeżanie danych...");

    String callsign = getCallsignFromCid(String(cid));
    if (callsign == "") {
      Serial.println("[MAIN] Brak callsign, przerywam");
      return;
    }

    VacdmSlotInfo vacdm_data = getVacdmData(callsign);

    displayData(callsign, vacdm_data);

    lastUpdate = millis();
  }
}


String getCallsignFromCid(String cid) {
  String url = "https://api.vatsim.net/v2/members/" + cid + "/status";
  HTTPClient http;
  http.begin(url);
  int code = http.GET();

  if (code != 200) {
    Serial.print("[VATSIM v2] Błąd HTTP: ");
    Serial.println(code);
    http.end();
    return "";
  }

  String payload = http.getString();
  http.end();

  Serial.println("[VATSIM v2] JSON:");
  Serial.println(payload);

  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.print("[VATSIM v2] Błąd JSON: ");
    Serial.println(error.c_str());
    return "";
  }


  if (!doc.containsKey("callsign")) {
    Serial.println("[VATSIM v2] CID offline / brak danych");
    return "";
  }

  return doc["callsign"].as<String>();
}



VacdmSlotInfo getVacdmData(String callsign) {
  VacdmSlotInfo slot;

  for (size_t i = 0; i < vacdm_server_count; ++i) {
    const VacdmServer& server = vacdm_servers[i];
    String url = server.scandinavianFormat
                   ? server.baseUrl
                   : server.baseUrl + "?callsign=" + callsign;

    Serial.print("[vACDM] Próba: ");
    Serial.println(url);

    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();

    if (httpCode != 200) {
      Serial.print("[vACDM] Błąd HTTP: ");
      Serial.println(httpCode);
      http.end();
      continue;
    }

    String payload = http.getString();
    http.end();

    DynamicJsonDocument doc(server.scandinavianFormat ? 16384 : 4096);
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
      Serial.print("[vACDM] Błąd JSON: ");
      Serial.println(err.c_str());
      continue;
    }

    if (server.scandinavianFormat) {

      JsonArray arr = doc.as<JsonArray>();
      bool found = false;

      for (size_t j = 0; j < arr.size(); ++j) {
        JsonObject item = arr[j];
        if (item["callsign"] == callsign) {
          slot.tobt = item["vacdm"]["tobt"] | "";
          slot.tsat = item["vacdm"]["tsat"] | "";
          slot.sid = item["clearance"]["sid"] | "---";

          if (item["clearance"].containsKey("dep_rwy")) {
            slot.runway = item["clearance"]["dep_rwy"] | "??";
            slot.hasRunway = true;
          }

          found = true;
          break;
        }
      }

      if (!found) {
        Serial.println("[vACDM] Callsign nie znaleziony w tablicy.");
        continue;
      }

    } else {

      slot.tobt = doc["tobt"] | "";
      slot.tsat = doc["tsat"] | "";
      slot.sid = doc["sid"] | "---";
      slot.hasRunway = false;
    }

    return slot;
  }

  Serial.println("[vACDM] Żaden serwer nie zwrócił poprawnych danych.");
  return slot;
}

String formatTimeShort(const String& timeStr) {
  if (timeStr.length() >= 16) {

    return timeStr.substring(11, 16) + "Z";
  } else if (timeStr.length() == 4) {

    return timeStr.substring(0, 2) + ":" + timeStr.substring(2, 4) + "Z";
  }
  return "--:--Z";
}



time_t parseIsoUtcTime(const String& input) {
  struct tm tm;
  time_t now = time(nullptr);
  gmtime_r(&now, &tm);

  if (input.length() >= 16) {

    tm.tm_year = input.substring(0, 4).toInt() - 1900;
    tm.tm_mon = input.substring(5, 7).toInt() - 1;
    tm.tm_mday = input.substring(8, 10).toInt();
    tm.tm_hour = input.substring(11, 13).toInt();
    tm.tm_min = input.substring(14, 16).toInt();
    tm.tm_sec = 0;
  } else if (input.length() == 4) {

    tm.tm_hour = input.substring(0, 2).toInt();
    tm.tm_min = input.substring(2, 4).toInt();
    tm.tm_sec = 0;
  } else {
    return 0;
  }

  tm.tm_isdst = 0;
  return mktime(&tm);
}


bool isAircraftAirborne(String cid) {
  if (SKIP_AIRBORNE_CHECK) return false;

  String url = "https://data.vatsim.net/v3/pilots/" + cid;
  HTTPClient http;
  http.begin(url);
  int code = http.GET();

  if (code != 200) {
    Serial.print("[AIRBORNE CHECK] Błąd HTTP: ");
    Serial.println(code);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.print("[AIRBORNE CHECK] Błąd JSON: ");
    Serial.println(error.c_str());
    return false;
  }

  int alt = doc["altitude"] | 0;
  int gs = doc["groundspeed"] | 0;

  Serial.printf("[AIRBORNE CHECK] ALT: %d ft, GS: %d kt\n", alt, gs);

  return (alt > 1000 && gs > 80);
}



void displayData(const String& callsign, const VacdmSlotInfo& slot) {
  uint16_t ledOrange = tft.color565(229, 135, 55);

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
  String sid = slot.sid;
  String rwy = slot.runway;


  int xCenter = 160;
  int y = 10;
  int lineSpacing = 30;

  tft.drawCentreString(callsign, xCenter, y, 1); y += lineSpacing;
  tft.drawCentreString("TOBT " + tobt, xCenter, y, 1); y += lineSpacing;
  tft.drawCentreString("TSAT " + tsat, xCenter, y, 1); y += lineSpacing;


  time_t nowUtc = time(nullptr);
  time_t tsatTime = parseIsoUtcTime(slot.tsat);
  int diffMin = (int)((nowUtc - tsatTime) / 60);
  String diffStr = (diffMin > 0 ? "+" : "") + String(diffMin);
  tft.drawCentreString(diffStr, xCenter, y, 1); y += lineSpacing;


  if (slot.hasRunway) {
    tft.drawCentreString("PLANNED RWY " + rwy, xCenter, y, 1); y += lineSpacing;
  }

  tft.drawCentreString("SID " + sid, xCenter, y, 1); y += lineSpacing;


  Serial.println("[vACDM] " + callsign + " | TOBT " + tobt + " | TSAT " + tsat + " | SID " + sid);
}