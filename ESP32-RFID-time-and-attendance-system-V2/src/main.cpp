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

enum mode_select { GREEN, RED, CHECK_ID, NONE };
mode_select currentMode = NONE;

const int sensorWaiting = 50;
const int sensorInLower = 1000;
const int sensorInUpper = 1200;
const int sensorOutLower = 200;
const int sensorOutUpper = 700;
const int sensorCheckLower = 650;
const int sensorCheckUpper = 950;

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
    case CHECK_ID: return "Sprawdź ID";
    default: return "Brak";
  }
}



mode_select readButton() {
  int sensorValue = analogRead(analogInPin);
  // Serial.println("analog: " + String(sensorValue)); // odkomentuj do debugowania
  if (sensorValue < sensorWaiting) return NONE;

  // Sprawdź czy w zakresie CHECK_ID (650..950)
  if (sensorValue >= sensorCheckLower && sensorValue <= sensorCheckUpper) return CHECK_ID;

  if (sensorValue >= sensorInLower && sensorValue <= sensorInUpper) return GREEN;
  else if (sensorValue >= sensorOutLower && sensorValue <= sensorOutUpper) return RED;
  return NONE;
}

// --- SZUKANIE użytkownika po UID ---
String findUser(String uid) {
  File usersFile = SD.open("/users.csv");
  if (!usersFile) return ";;"; // brak użytkownika

  while (usersFile.available()) {
    String line = usersFile.readStringUntil('\n');
    line.trim();
    if (line.startsWith(uid + ";")) {
      usersFile.close();
      // Format: UID;imie;nazwisko;podzespół
      int first = line.indexOf(';');
      int second = line.indexOf(';', first + 1);
      int third = line.indexOf(';', second + 1);
      String imie = line.substring(first + 1, second);
      String nazwisko = line.substring(second + 1, third);
      String podzesp = line.substring(third + 1);
      return imie + ";" + nazwisko + ";" + podzesp;
    }
  }
  usersFile.close();
  return ";;";
}

// --- ZAPIS KARTY DO LOGA ---
void logCardToSD(String uid) {
  String timestamp = String(RTC.getYear()) + "-" +
                     String(RTC.getMonth(century)) + "-" +
                     String(RTC.getDate()) + " " +
                     String(RTC.getHour(h12Flag, pmFlag)) + ":" +
                     String(RTC.getMinute()) + ":" +
                     String(RTC.getSecond());

  String userData = findUser(uid);
  File logFile = SD.open("/log.txt", FILE_WRITE);
  if (logFile) {
    logFile.println(timestamp + ";" + uid + ";" + userData + ";" + modeToString(currentMode));
    logFile.close();
    Serial.println("Zapisano: " + timestamp + " UID:" + uid + " -> " + userData);
  } else {
    Serial.println("Błąd zapisu log.txt");
  }
}
// --- POBIERANIE LISTY UŻYTKOWNIKÓW DO DROPLISTY ---
String getUserOptions() {
  File usersFile = SD.open("/users.csv");
  String options = "";
  if (!usersFile) return options;

  while (usersFile.available()) {
    String line = usersFile.readStringUntil('\n');
    line.trim();
    if (line.length() < 3) continue;

    int first = line.indexOf(';');
    int second = line.indexOf(';', first + 1);
    int third = line.indexOf(';', second + 1);

    String uid = line.substring(0, first);
    String imie = line.substring(first + 1, second);
    String nazwisko = line.substring(second + 1, third);
    String label = imie + " " + nazwisko + " (" + uid + ")";
    options += "<option value='" + uid + "'>" + label + "</option>";
  }
  usersFile.close();
  return options;
}
// --- USUWANIE UŻYTKOWNIKA PO UID ---
void handleDeleteUser() {
  String uid = server.arg("uid");
  if (uid == "") {
    server.send(400, "text/plain", "Brak UID do usunięcia!");
    return;
  }

  File oldFile = SD.open("/users.csv");
  String newContent = "";
  if (oldFile) {
    while (oldFile.available()) {
      String line = oldFile.readStringUntil('\n');
      if (!line.startsWith(uid + ";")) newContent += line + "\n";
    }
    oldFile.close();
  }

  SD.remove("/users.csv");
  File newFile = SD.open("/users.csv", FILE_WRITE);
  if (newFile) {
    newFile.print(newContent);
    newFile.close();
  }

  server.sendHeader("Location", "/");
  server.send(303);
}

// --- STRONA GŁÓWNA ---
void handleRoot() {
  String html = "<html><head><meta charset='utf-8'><title>Rejestr RFID</title></head><body>";
  html += "<h2>POLSL RACING- Rejestr wejść/wyjść</h2>";
  html += "<p>Aktualny tryb: <b>" + modeToString(currentMode) + "</b></p>";
  html += "<a href='/setMode?mode=GREEN'><button>Tryb Wejście</button></a>";
  html += "<a href='/setMode?mode=RED'><button>Tryb Wyjście</button></a>";
  html += "<a href='/setMode?mode=CHECK_ID'><button>Tryb Sprawdź ID</button></a>";

  // --- Formularz dodania użytkownika ---
  html += "<hr><h3>Dodaj / Przypisz użytkownika</h3>";
  html += "<form action='/addUser' method='GET'>";
  html += "UID: <input name='uid'><br>";
  html += "Imię: <input name='imie'><br>";
  html += "Nazwisko: <input name='nazwisko'><br>";
  html += "Podzespół: <input name='podzesp'><br>";
  html += "<input type='submit' value='Zapisz użytkownika'>";
  html += "</form>";
  // --- Formularz usuwania użytkownika ---
  html += "<hr><h3>Usuń użytkownika</h3>";
  html += "<form action='/deleteUser' method='GET'>";
  html += "Wybierz użytkownika: <select name='uid'>";
  html += getUserOptions();
  html += "</select><br>";
  html += "<input type='submit' value='Usuń użytkownika' ";
  html += "style='background-color:red;color:white;' ";
  html += "onclick=\"return confirm('Na pewno usunąć wybranego użytkownika?');\">";
  html += "</form>";

  // --- Logi ---
  html += "<hr><h3>Logi (Data;UID;Imię;Nazwisko;Podzespół;Tryb):</h3><pre>";
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
  html += "<button style='background-color:red;color:white;' onclick=\"if(confirm('Czy na pewno chcesz wyczyścić wszystkie logi?')) window.location='/clearData';\">Wyczyść dane</button>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// --- DODANIE UŻYTKOWNIKA DO PLIKU ---
void handleAddUser() {
  String uid = server.arg("uid");
  String imie = server.arg("imie");
  String nazwisko = server.arg("nazwisko");
  String podzesp = server.arg("podzesp");

  if (uid == "") {
    server.send(400, "text/plain", "Brak UID!");
    return;
  }

  // Usuń poprzedni wpis o tym samym UID
  File oldFile = SD.open("/users.csv");
  String newContent = "";
  if (oldFile) {
    while (oldFile.available()) {
      String line = oldFile.readStringUntil('\n');
      if (!line.startsWith(uid + ";")) newContent += line + "\n";
    }
    oldFile.close();
  }

  File newFile = SD.open("/users.csv", FILE_WRITE);
  if (newFile) {
    newFile.print(newContent);
    newFile.println(uid + ";" + imie + ";" + nazwisko + ";" + podzesp);
    newFile.close();
  }

  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSetMode() {
  String m = server.arg("mode");
  if (m == "GREEN") currentMode = GREEN;
  else if (m == "RED") currentMode = RED;
  else currentMode = NONE;
  handleRoot();
}
// --- WYCZYSZCZENIE DANYCH (logów) ---
void handleClearData() {
  // Usuń plik logów
  if (SD.exists("/log.txt")) {
    SD.remove("/log.txt");
    File newLog = SD.open("/log.txt", FILE_WRITE);
    if (newLog) {
      newLog.println("Data;UID;Imię;Nazwisko;Podzespół;Tryb");
      newLog.close();
    }
  }

  // Opcjonalnie można usunąć inne pliki, ale users.csv zostaje
  // SD.remove("/inne_dane.txt");

  server.sendHeader("Location", "/");
  server.send(303);
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
  server.on("/addUser", handleAddUser);
  server.on("/download", handleDownload);
  server.on("/clearData", handleClearData);
  server.on("/deleteUser", handleDeleteUser);


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
    Serial.print("Tryb: "); Serial.println(modeToString(currentMode));

    if (currentMode == CHECK_ID) {
      // SPRAWDZANIE: czy UID jest w users.csv
      String userData = findUser(uid);
      if (userData != ";;") {
        // znaleziono
        Serial.println("KARTA PRZYPISANA -> " + userData);
      } else {
        Serial.println("KARTA: BRAK PRZYPISANIA");
      }
      // nadal zapisujemy do logu, ale tryb będzie "Sprawdź ID"
      logCardToSD(uid);
    } else {
      // normalne logowanie dla WEJŚCIE/WYJŚCIE/itd.
      logCardToSD(uid);
    }

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }
}
