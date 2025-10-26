#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <DS3231.h>
#include <Wire.h>
#include <SD.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>


#define RFID_SS_PIN D8
#define SD_SS_PIN D4
#define RST_PIN D1
#define ON_Board_LED 2
const int analogInPin = A0;

bool century = false;
bool h12Flag;
bool pmFlag;

unsigned long previousMillis = 0;
const long interval = 100;

enum mode_select { GREEN, RED, BLUE, NONE };
mode_select currentMode = NONE;

const int sensorWaiting = 50;
const int sensorInLower = 1000;
const int sensorInUpper = 1200;
const int sensorOutLower = 200;
const int sensorOutUpper = 700;
const int sensorPeopleLower = 700;
const int sensorPeopleUpper = 950;

MFRC522 rfid(RFID_SS_PIN, RST_PIN);
DS3231 RTC;
File logFile;

ESP8266WebServer server(80);

const char* ssid = "ESP32_RFID";
const char* password = "12345678";

String modeToString(mode_select mode) {
  switch (mode) {
    case GREEN: return "Wejście";
    case RED: return "Wyjście";
    case BLUE: return "Rejestracja";
    default: return "Brak";
  }
}

mode_select readButton() {
  int sensorValue = analogRead(analogInPin);
  if (sensorValue < sensorWaiting) return NONE;
  if (sensorValue > sensorInLower && sensorValue < sensorInUpper) return GREEN;
  else if (sensorValue > sensorOutLower && sensorValue < sensorOutUpper) return RED;
  else if (sensorValue >= sensorPeopleLower && sensorValue <= sensorPeopleUpper) return BLUE;
  return NONE;
}

void logCardToSD(String uid) {
  String timestamp = String(RTC.getYear()) + "-" +
                     String(RTC.getMonth(century)) + "-" +
                     String(RTC.getDate()) + " " +
                     String(RTC.getHour(h12Flag, pmFlag)) + ":" +
                     String(RTC.getMinute()) + ":" +
                     String(RTC.getSecond());

  File logFile = SD.open("/log.txt", FILE_WRITE);
  if (logFile) {
    logFile.println(timestamp + ";" + uid + ";" + modeToString(currentMode));
    logFile.close();
    Serial.println("Zapisano: " + timestamp + " UID:" + uid + " Tryb:" + modeToString(currentMode));
  } else {
    Serial.println("Błąd zapisu log.txt");
  }
}

void handleRoot() {
  String html = "<html><head><meta charset='utf-8'><title>Rejestr RFID</title></head><body>";
  html += "<h2>ESP32 RFID - Rejestr wejść/wyjść</h2>";
  html += "<p>Aktualny tryb: <b>" + modeToString(currentMode) + "</b></p>";
  html += "<a href='/setMode?mode=GREEN'><button>Tryb Wejście</button></a>";
  html += "<a href='/setMode?mode=RED'><button>Tryb Wyjście</button></a>";
  html += "<a href='/setMode?mode=BLUE'><button>Tryb Rejestracja</button></a>";
  html += "<hr><h3>Logi:</h3><pre>";

  File logFile = SD.open("/log.txt");
  if (logFile) {
    while (logFile.available()) {
      html += logFile.readStringUntil('\n');
    }
    logFile.close();
  } else {
    html += "Brak logów.";
  }

  html += "</pre><hr>";
  html += "<a href='/download'><button>Pobierz CSV</button></a>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleSetMode() {
  String m = server.arg("mode");
  if (m == "GREEN") currentMode = GREEN;
  else if (m == "RED") currentMode = RED;
  else if (m == "BLUE") currentMode = BLUE;
  else currentMode = NONE;
  handleRoot();
}

void handleDownload() {
  File logFile = SD.open("/log.txt");
  if (!logFile) {
    server.send(404, "text/plain", "Brak log.txt");
    return;
  }
  server.streamFile(logFile, "text/csv");
  logFile.close();
}

void setup() {
  Serial.begin(115200);
  SPI.begin();
  Wire.begin();
  rfid.PCD_Init();
  pinMode(ON_Board_LED, OUTPUT);
  digitalWrite(ON_Board_LED, HIGH);

  if (!SD.begin(SD_SS_PIN)) {
    Serial.println("Błąd inicjalizacji SD!");
  } else {
    Serial.println("SD gotowe.");
  }

  WiFi.softAP(ssid, password);
  Serial.println("AP uruchomiony. Połącz się z siecią: " + String(ssid));
  Serial.println("Adres strony: http://192.168.4.1");

  server.on("/", handleRoot);
  server.on("/setMode", handleSetMode);
  server.on("/download", handleDownload);
  server.begin();
}

void loop() {
  server.handleClient();

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    mode_select newMode = readButton();
    if (newMode != NONE) currentMode = newMode;
  }

  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    digitalWrite(ON_Board_LED, HIGH);
    delay(100);
    digitalWrite(ON_Board_LED, LOW);

    String uid = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      if (rfid.uid.uidByte[i] < 0x10) uid += "0";
      uid += String(rfid.uid.uidByte[i], HEX);
      if (i < rfid.uid.size - 1) uid += ":";
    }

    Serial.print("Karta UID: "); Serial.println(uid);
    logCardToSD(uid);

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }
}
