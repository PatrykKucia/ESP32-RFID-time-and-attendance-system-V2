#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <DS3231.h>
#include <Wire.h>
#include <SD.h>

#define RFID_SS_PIN D8
#define SD_SS_PIN D4    // CS karty SD
#define RST_PIN D1
#define ON_Board_LED 2 
const int analogInPin = A0;       //Pin analogowy do obsługi przycisków

bool century = false;
bool h12Flag;
bool pmFlag;

unsigned long previousMillis = 0;
const long interval = 100; // co 200 ms sprawdzamy przyciskenum mode_select { NONE, IN, PEOPLE_IN, REG };

enum mode_select { GREEN, RED, BLUE, NONE };
// progi analogowe (dostosuj do swojego potencjometru/układu)
const int sensorWaiting = 50;      
const int sensorInLower = 1000;
const int sensorInUpper = 1200;
const int sensorOutLower = 200;
const int sensorOutUpper = 700;
const int sensorPeopleLower = 700;
const int sensorPeopleUpper = 950;


// --- RFID i RTC ---
MFRC522 rfid(RFID_SS_PIN, RST_PIN);
DS3231 RTC;

// --- ustawienie czasu raz ---
bool setTimeOnce = false;  // ustaw na true przy pierwszym wgraniu

File logFile;

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

  File logFile = SD.open("log.txt", FILE_WRITE);
  if (logFile) {
    logFile.println(timestamp + " UID:" + uid);
    logFile.close();
    Serial.println("Logged to SD: " + timestamp + " UID:" + uid);
  } else {
    Serial.println("Error opening log.txt");
  }
}

void setup() {
  Serial.begin(115200);
  SPI.begin();
  Wire.begin();
  rfid.PCD_Init();

  pinMode(ON_Board_LED, OUTPUT);
  digitalWrite(ON_Board_LED, HIGH);

  Serial.println("RFID ready - scan a card...");
  if (!rfid.PCD_PerformSelfTest()) {
    Serial.println("MFRC522 self test FAILED");
  } else {
      Serial.println("MFRC522 self test OK");
  }

  // --- ustawienie czasu na RTC raz ---
  if (setTimeOnce) {
      // Przykładowy czas: 16 sierpnia 2022, 10:00:00
      RTC.setYear(25);
      RTC.setMonth(10);
      RTC.setDate(26);
      RTC.setHour(13);
      RTC.setMinute(39);
      RTC.setSecond(0);
      RTC.setDoW(0);       // 0 = niedziela, 1 = poniedziałek, 2 = wtorek ...
      RTC.setClockMode(false); // 24h
      Serial.println("RTC time set. Comment this block after first upload.");
      // setTimeOnce = false; // po pierwszym ustawieniu możesz zakomentować lub ustawić false
  }
    // --- inicjalizacja SD ---
  if (!SD.begin(SD_SS_PIN)) {
    Serial.println("SD card initialization failed!");
  } else {
    Serial.println("SD card initialized.");
  }
}


void loop() 
{
unsigned long currentMillis = millis();
  // --- odczyt przycisku co 200ms ---
  if (currentMillis - previousMillis >= interval) {
      previousMillis = currentMillis;
      mode_select currentMode = readButton();

      switch (currentMode) {
          case GREEN:
              Serial.println("Mode:GREEN");
              break;
          case RED:
              Serial.println("Mode:RED");
              break;
          case BLUE:
              Serial.println("Mode:BLUE");
              break;
          default:
              break;
      }
  }

  // --- odczyt RFID ---
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      digitalWrite(ON_Board_LED, HIGH);
      delay(150);
      digitalWrite(ON_Board_LED, LOW);

      String uid = "";
      for (byte i = 0; i < rfid.uid.size; i++) {
          if (rfid.uid.uidByte[i] < 0x10) uid += "0";
          uid += String(rfid.uid.uidByte[i], HEX);
          if (i < rfid.uid.size - 1) uid += ":";
      }

      Serial.print("Card UID: "); Serial.println(uid);

      MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);
      Serial.print("Type: ");
      Serial.println(rfid.PICC_GetTypeName(piccType));

      logCardToSD(uid); // zapis UID + timestamp na SD

      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
  }

  // --- odczyt czasu z RTC co 1s ---
  static unsigned long lastTimeMillis = 0;
  if (millis() - lastTimeMillis >= 1000) {
      lastTimeMillis = millis();
      Serial.print("RTC Time: ");
      Serial.print(RTC.getYear(), DEC); Serial.print("-");
      Serial.print(RTC.getMonth(century), DEC); Serial.print("-");
      Serial.print(RTC.getDate(), DEC); Serial.print(" ");
      Serial.print(RTC.getHour(h12Flag, pmFlag), DEC); Serial.print(":");
      Serial.print(RTC.getMinute(), DEC); Serial.print(":");
      Serial.println(RTC.getSecond(), DEC);
  }
}
