#ifndef GFXFF
#define GFXFF 1
#endif

#include <TFT_eSPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "doto-regular18pt7b.h"  // VDGS FONT
#include <time.h>
#include "include/config.h"


TFT_eSPI tft = TFT_eSPI();

const String vatsim_data_url = "https://data.vatsim.net/v3/vatsim-data.json";
const String vacdm_servers[] = {
  "https://app.vacdm.net",
  "https://vacdm.vatita.net",
  "https://cdm.vatsim-scandinavia.org",
  "https://cdm.vatsim.fr",
  "https://vacdm.vatprc.net",
  "https://vacdm.vacc-austria.org",
  "https://vacdm.vatsim.me"
};
const size_t vacdm_server_count = sizeof(vacdm_servers) / sizeof(vacdm_servers[0]);

String getCallsignFromCid(String cid);
String getVacdmData(String callsign);
void displayData(const String& callsign, const String& dataJson);

unsigned long lastUpdate = 0;
const unsigned long refreshInterval = 30000; // 30 seconds

String cid = VATSIM_CID;

void setup() {
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);
  // NTP
  configTime(0, 0, "pool.ntp.org"); // set NTP server and UTC time


  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  // tft.setTextColor(TFT_WHITE, TFT_BLACK);
  // tft.setTextSize(2);
  
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

  String vacdm_data = getVacdmData(callsign);
  displayData(callsign, vacdm_data);
}

void loop() {
  if (millis() - lastUpdate > refreshInterval) {
    Serial.println("[MAIN] Odświeżanie danych...");

    String callsign = getCallsignFromCid(String(cid)); // użyj zapamiętanego CID
    if (callsign == "") {
      Serial.println("[MAIN] Brak callsign, przerywam");
      return;
    }

    String vacdm_data = getVacdmData(callsign);
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

  DynamicJsonDocument doc(4096);  // JSON nie jest duży
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.print("[VATSIM v2] Błąd JSON: ");
    Serial.println(error.c_str());
    return "";
  }

  // jeśli brak "callsign", to znaczy że user offline
  if (!doc.containsKey("callsign")) {
    Serial.println("[VATSIM v2] CID offline / brak danych");
    return "";
  }

  return doc["callsign"].as<String>();
}



String getVacdmData(String callsign) {
  for (size_t i = 0; i < vacdm_server_count; ++i) {
    String url = vacdm_servers[i] + "/api/v1/pilots/" + callsign;
    Serial.print("[vACDM] Próba: ");
    Serial.println(url);

    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();

    if (httpCode == 200) {
      String payload = http.getString();
      http.end();
      Serial.println("[vACDM] OK");
      return payload;
    } else {
      Serial.print("[vACDM] Błąd HTTP: ");
      Serial.println(httpCode);
    }
    http.end();
  }

  Serial.println("[vACDM] Żaden serwer nie zwrócił danych.");
  return "";
}

String formatTimeShort(const String& isoString) {
  // Oczekiwany format: 2025-05-02T11:30:00.000Z
  if (isoString.length() < 16) return "--:--Z";
  return isoString.substring(11, 16) + "Z";
}


time_t parseIsoUtcTime(const String& iso) {
  if (iso.length() < 16) return 0;

  struct tm tm;
  tm.tm_year = iso.substring(0, 4).toInt() - 1900;
  tm.tm_mon  = iso.substring(5, 7).toInt() - 1;
  tm.tm_mday = iso.substring(8, 10).toInt();
  tm.tm_hour = iso.substring(11, 13).toInt();
  tm.tm_min  = iso.substring(14, 16).toInt();
  tm.tm_sec  = 0;
  tm.tm_isdst = 0;

  return mktime(&tm); // w lokalnym czasie, ale zaraz to poprawimy
}

bool isAircraftAirborne(String cid) {
  if (SKIP_AIRBORNE_CHECK) return false;  // nie sprawdzamy

  String url = "https://data.vatsim.net/v3/pilots/" + cid;
  HTTPClient http;
  http.begin(url);
  int code = http.GET();

  if (code != 200) {
    Serial.print("[AIRBORNE CHECK] Błąd HTTP: ");
    Serial.println(code);
    http.end();
    return false;  // zakładamy że NIE wystartował
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
  int gs  = doc["groundspeed"] | 0;

  Serial.printf("[AIRBORNE CHECK] ALT: %d ft, GS: %d kt\n", alt, gs);

  return (alt > 1000 && gs > 80);
}



void displayData(const String& callsign, const String& dataJson) {
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

  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, dataJson);
  if (error) {
    tft.setCursor(10, 50);
    tft.println("JSON ERROR");
    Serial.println("[vACDM] Błąd JSON: " + String(error.c_str()));
    return;
  }

  String tobt = formatTimeShort(doc["vacdm"]["tobt"] | "");
  String tsat = formatTimeShort(doc["vacdm"]["tsat"] | "");
  int exot = doc["vacdm"]["exot"] | 0;
  String rwy = doc["clearance"]["dep_rwy"] | "??";
  String sid = doc["clearance"]["sid"] | "---";

  // Pozycje pionowe (dostosuj w razie potrzeby)
  int xCenter = 160;
  int y = 10;
  int lineSpacing = 30;

  tft.drawCentreString(callsign, xCenter, y, 1);         y += lineSpacing;
  tft.drawCentreString("TOBT " + tobt, xCenter, y, 1);   y += lineSpacing;
  tft.drawCentreString("TSAT " + tsat, xCenter, y, 1);   y += lineSpacing;
  // tft.drawCentreString(String(exot), xCenter, y, 1);     y += lineSpacing; // Wywalamy EXOT
  time_t nowUtc = time(nullptr);
  time_t tsatTime = parseIsoUtcTime(doc["vacdm"]["tsat"] | "");

  int diffMin = (int)((nowUtc - tsatTime) / 60);
  String diffStr = (diffMin > 0 ? "+" : "") + String(diffMin); 

  tft.drawCentreString(diffStr, xCenter, y, 1); y += lineSpacing;

  tft.drawCentreString("PLANNED RWY " + rwy, xCenter, y, 1); y += lineSpacing;
  tft.drawCentreString("SID " + sid, xCenter, y, 1);     y += lineSpacing;

  // Debug
  Serial.println("[vACDM] " + callsign + " | TOBT " + tobt + " | TSAT " + tsat + " | EXOT " + String(exot));
}