#ifndef GFXFF
#define GFXFF 1
#endif

#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "doto-regular18pt7b.h"  // VDGS FONT
#include <time.h>
#include "include/config.h"

WiFiMulti wifiMulti;

TFT_eSPI tft = TFT_eSPI();

struct VacdmSlotInfo {
  String tobt = "";
  String tsat = "";
  String sid = "";
  String runway = "";
  bool hasRunway = false;
};


// const String vatsim_data_url = "https://data.vatsim.net/v3/vatsim-data.json";
struct VacdmServer {
  String baseUrl;
  bool scandinavianFormat;  // true = /api/v1/pilots/CALLSIGN, false = ?callsign=CALLSIGN
};

const VacdmServer vacdm_servers[] = {
  { "https://app.vacdm.net/api/v1/pilots", true },
  { "https://vacdm.vatita.net/api/v1/pilots", true },
  { "https://cdm.vatsim-scandinavia.org/api/v1/pilots", true },
  { "https://cdm.vatsim.fr/api/v1/pilots", true },
  { "https://vacdm.vatprc.net/api/v1/pilots", true },
  { "https://vacdm.vacc-austria.org/api/v1/pilots", true },
  { "https://cdm-server-production.up.railway.app/slotService/callsign", false } // Hiszpania
};

const size_t vacdm_server_count = sizeof(vacdm_servers) / sizeof(vacdm_servers[0]);


String getCallsignFromCid(String cid);
// String getVacdmData(String callsign);
//void displayData(const String& callsign, const String& dataJson);
void displayData(const String& callsign, const VacdmSlotInfo& slot);


unsigned long lastUpdate = 0;
int offlineCount = 0;
const int offlineThreshold = 3;
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

 
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString("VDGS Display", 160, 40, 1);
  tft.drawCentreString("by PLVACC",    160, 75, 1);


  connectToWiFi();

  if (WiFi.status() != WL_CONNECTED) {
    tft.println("No WiFi available");
    return;
  }

  delay(1000);
  //Check if CID is online
  String callsign = getCallsignFromCid(cid);
if (callsign == "") {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setFreeFont(&doto_regular18pt7b);
  tft.drawCentreString("Waiting for", 160, 60, 1);    // x=160 = środek 320px
  tft.drawCentreString("login...",     160, 90, 1);
  
  return;
}

  VacdmSlotInfo vacdm_slot = getVacdmData(callsign);
  displayData(callsign, vacdm_slot);
}

void loop() {
  if (millis() - lastUpdate > refreshInterval) {
    Serial.println("[MAIN] Odświeżanie danych...");

    String callsign = getCallsignFromCid(cid);
    if (callsign == "") {
      offlineCount++;
      Serial.printf("[MAIN] CID offline (attempt %d)\n", offlineCount);
    
      tft.fillScreen(TFT_BLACK);
      tft.setCursor(20, 60);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.setFreeFont(&doto_regular18pt7b);
      
      if (offlineCount >= offlineThreshold) {
        tft.drawCentreString("Waiting for", 160, 60, 1);
        tft.drawCentreString("login...",     160, 90, 1);
      } else {
        tft.drawCentreString("User NOT",     160, 60, 1);
        tft.drawCentreString("logged IN!",   160, 90, 1);
      }
    
      lastUpdate = millis();
      return;
    } else {
      if (offlineCount > 0) {
        Serial.println("[MAIN] CID zalogowany, reset licznika offline");
      }
      offlineCount = 0;
    }
    


    // String callsign = getCallsignFromCid(String(cid)); // użyj zapamiętanego CID
    //if (callsign == "") {
    //  Serial.println("[MAIN] Brak callsign, przerywam");
    //  return;
    // }

    VacdmSlotInfo vacdm_data = getVacdmData(callsign);

    displayData(callsign, vacdm_data);

    lastUpdate = millis();
  }
}

// Functions from here:


// Multiple Wireless networks
void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID_1, WIFI_PASSWORD_1);
  Serial.print("Connecting to network: ");
  Serial.println(WIFI_SSID_1);

  unsigned long startAttemptTime = millis();

  // Czekaj na połączenie przez 10 sekund
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nNetwork 1 connected");
    return;
  }

  // Próba połączenia z drugą siecią
  WiFi.begin(WIFI_SSID_2, WIFI_PASSWORD_2);
  Serial.print("\nConnecting to network: ");
  Serial.println(WIFI_SSID_2);

  startAttemptTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nNetwork 2 connected");
  } else {
    Serial.println("\nUnable to connect to WiFi");
  }
}


// Get callsing for logged user
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


// Get ACDM Data

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
      // Format tablicowy — musimy znaleźć callsign
      JsonArray arr = doc.as<JsonArray>();
      bool found = false;

      for (size_t j = 0; j < arr.size(); ++j) {
        JsonObject item = arr[j];
        if (item["callsign"] == callsign) {
          slot.tobt = item["vacdm"]["tobt"] | "";
          slot.tsat = item["vacdm"]["tsat"] | "";
          slot.sid  = item["clearance"]["sid"] | "---";

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
      // Format prosty (Hiszpania)
      slot.tobt = doc["tobt"] | "";
      slot.tsat = doc["tsat"] | "";
      slot.sid  = doc["sid"] | "---";
      slot.hasRunway = false;
    }

    return slot;
  }

  Serial.println("[vACDM] Żaden serwer nie zwrócił poprawnych danych.");
  return slot;
}

// Time shortening func
String formatTimeShort(const String& isoString) {
  // Sprawdź, czy isoString to wartość domyślna
  if (isoString == "1969-12-31T23:59:59.999Z") {
    return "--:--";
  }
  // Sprawdź długość ciągu
  if (isoString.length() < 16) return "--:--Z";
  return isoString.substring(11, 16) + "Z";
}

// Time to  ZULU
time_t parseIsoUtcTime(const String& input) {
  struct tm tm;
  time_t now = time(nullptr);
  gmtime_r(&now, &tm); // ustaw aktualną datę (UTC)

  if (input.length() >= 16) {
    // Format ISO: "2025-05-02T11:30:00.000Z"
    tm.tm_year = input.substring(0, 4).toInt() - 1900;
    tm.tm_mon  = input.substring(5, 7).toInt() - 1;
    tm.tm_mday = input.substring(8, 10).toInt();
    tm.tm_hour = input.substring(11, 13).toInt();
    tm.tm_min  = input.substring(14, 16).toInt();
    tm.tm_sec  = 0;
  } else if (input.length() == 4) {
    // Format HHMM (np. "1630") — zakładamy dziś UTC
    tm.tm_hour = input.substring(0, 2).toInt();
    tm.tm_min  = input.substring(2, 4).toInt();
    tm.tm_sec  = 0;
  } else {
    return 0;
  }

  tm.tm_isdst = 0;
  return mktime(&tm);
}

// Check if user aircraft has departed 
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


// Display data

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

  // Dane z gotowej struktury
  String tobt = formatTimeShort(slot.tobt);
  String tsat = formatTimeShort(slot.tsat);
  String sid  = slot.sid;
  String rwy  = slot.runway;

  // Pozycje pionowe
  int xCenter = 160;
  int y = 10;
  int lineSpacing = 30;

  tft.drawCentreString(callsign, xCenter, y, 1);         y += lineSpacing;
  tft.drawCentreString("TOBT " + tobt, xCenter, y, 1);   y += lineSpacing;
  tft.drawCentreString("TSAT " + tsat, xCenter, y, 1);   y += lineSpacing;

  // Różnica czasu do TSAT
  time_t nowUtc = time(nullptr);
  time_t tsatTime = parseIsoUtcTime(slot.tsat);
  int diffMin = (int)((nowUtc - tsatTime) / 60);
  String diffStr = (diffMin > 0 ? "+" : "") + String(diffMin);
  tft.drawCentreString(diffStr, xCenter, y, 1);          y += lineSpacing;

  // RWY tylko jeśli dostępna
  if (slot.hasRunway) {
    tft.drawCentreString("PLANNED RWY " + rwy, xCenter, y, 1); y += lineSpacing;
  }

  tft.drawCentreString("SID " + sid, xCenter, y, 1);     y += lineSpacing;

  // Debug
  Serial.println("[vACDM] " + callsign + " | TOBT " + tobt + " | TSAT " + tsat + " | SID " + sid);
}
