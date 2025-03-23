// By Fatih Nurrobi Alansshori Teknik Komputer UPI

#include <Arduino.h>
#include <WiFiManager.h>
#include <Wire.h>
#include <RTClib.h>

// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define RELAY1 1
#define RELAY2 2
#define RELAY3 41
#define RELAY4 42
#define RELAY5 45
#define RELAY6 46

// Tentukan pin I2C secara eksplisit (sesuaikan dengan pin yang Anda gunakan)
#define SDA_PIN 8
#define SCL_PIN 9

// Inisialisasi objek RTC
RTC_DS3231 rtc;
char daysOfTheWeek[7][12] = {"Minggu", "Senin", "Selasa", "Rabu", "Kamis", "Jumat", "Sabtu"};

// Mode operasi
enum OperationMode {
  MODE_ISIBAK,
  MODE_MIXING,
  MODE_SUPPLY,
  MODE_OFF,
  MODE_AUTO  // Mode otomatis untuk menjalankan semua secara berurutan
};

// Variabel untuk mode operasi saat ini
OperationMode currentMode = MODE_OFF;

// Mutex untuk akses ke relay
SemaphoreHandle_t relayMutex;

// Deklarasi task
void TaskLocalOperation(void *pvParameters);
void TaskAutoSequence(void *pvParameters);
void TaskWiFiSetup(void *pvParameters);
void TaskSerialCommand(void *pvParameters);
void TaskRTCMonitor(void *pvParameters);

// Fungsi relay
void isibak();
void mixing();
void supply();
void relayoff();
void setOperationMode(OperationMode mode);
const char* getModeName(OperationMode mode);
bool isScheduledTime();

// Format waktu menjadi string
String formatDateTime(const DateTime &dt) {
  char buffer[25];
  sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d",
          dt.year(), dt.month(), dt.day(),
          dt.hour(), dt.minute(), dt.second());
  return String(buffer);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Silagung Controller Starting...");

  // Inisialisasi mutex
  relayMutex = xSemaphoreCreateMutex();

  // Inisialisasi pin relay
  pinMode(RELAY1, OUTPUT);  
  pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT);
  pinMode(RELAY4, OUTPUT);
  pinMode(RELAY5, OUTPUT);
  pinMode(RELAY6, OUTPUT);

  // Matikan semua relay pada awalnya
  relayoff();

  // Inisialisasi I2C dan RTC
  Wire.begin(SDA_PIN, SCL_PIN);
  
  // Cek apakah RTC terdeteksi
  if (!rtc.begin()) {
    Serial.println("Tidak dapat menemukan RTC DS3231!");
    Serial.println("Periksa koneksi kabel dan pastikan baterai terpasang");
  } else {
    // Cek apakah RTC kehilangan daya dan perlu diatur ulang
    if (rtc.lostPower()) {
      Serial.println("RTC kehilangan daya, mengatur waktu ke waktu kompilasi!");
      // Atur RTC ke waktu kompilasi
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    
    // Tampilkan waktu saat ini
    DateTime now = rtc.now();
    Serial.print("Waktu saat ini: ");
    Serial.println(formatDateTime(now));
    Serial.print("Hari: ");
    Serial.println(daysOfTheWeek[now.dayOfTheWeek()]);
    
  }

  // Buat task
  xTaskCreate(
    TaskLocalOperation,    // Fungsi task
    "LocalOperation",      // Nama task
    4096,                  // Stack size
    NULL,                  // Parameter
    1,                     // Prioritas
    NULL                   // Task handle
  );

  xTaskCreate(
    TaskAutoSequence,      // Fungsi task
    "AutoSequence",        // Nama task
    2048,                  // Stack size
    NULL,                  // Parameter
    1,                     // Prioritas
    NULL                   // Task handle
  );

  xTaskCreate(
    TaskWiFiSetup,         // Fungsi task
    "WiFiSetup",           // Nama task
    4096,                  // Stack size
    NULL,                  // Parameter
    2,                     // Prioritas (lebih tinggi untuk setup WiFi)
    NULL                   // Task handle
  );

  // Tambahkan task untuk membaca perintah serial
  xTaskCreate(
    TaskSerialCommand,     // Fungsi task
    "SerialCommand",       // Nama task
    2048,                  // Stack size
    NULL,                  // Parameter
    1,                     // Prioritas
    NULL                   // Task handle
  );
  
  // Tambahkan task untuk monitoring RTC
  xTaskCreate(
    TaskRTCMonitor,        // Fungsi task
    "RTCMonitor",          // Nama task
    2048,                  // Stack size
    NULL,                  // Parameter
    1,                     // Prioritas
    NULL                   // Task handle
  );
}

void loop() {
  // Kosong karena kita menggunakan FreeRTOS tasks
  vTaskDelay(1000 / portTICK_PERIOD_MS); // Beri waktu untuk task lain
}

// Task untuk operasi lokal (akan berjalan bahkan saat offline)
void TaskLocalOperation(void *pvParameters) {
  (void) pvParameters;
  
  for (;;) {
    // Ambil mutex sebelum mengakses relay
    if (xSemaphoreTake(relayMutex, portMAX_DELAY) == pdTRUE) {
      // Jalankan mode operasi saat ini (kecuali mode AUTO yang ditangani oleh task lain)
      if (currentMode != MODE_AUTO) {
        switch (currentMode) {
          case MODE_ISIBAK:
            isibak();
            break;
          case MODE_MIXING:
            mixing();
            break;
          case MODE_SUPPLY:
            supply();
            break;
          case MODE_OFF:
            relayoff();
            break;
          default:
            break;
        }
      }
      
      // Lepaskan mutex
      xSemaphoreGive(relayMutex);
    }
    
    // Delay sebelum iterasi berikutnya
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// Task untuk menjalankan sekuens otomatis
void TaskAutoSequence(void *pvParameters) {
  (void) pvParameters;
  
  for (;;) {
    // Hanya jalankan jika dalam mode AUTO
    if (currentMode == MODE_AUTO) {
      // Cek apakah sekarang adalah waktu terjadwal (7 pagi atau 4 sore)
      if (isScheduledTime()) {
        Serial.println("Waktu terjadwal terdeteksi, memulai sekuens otomatis");
        
        // Ambil mutex sebelum mengakses relay
        if (xSemaphoreTake(relayMutex, portMAX_DELAY) == pdTRUE) {
          // Jalankan sekuens
          Serial.println("Auto Sequence: Isi Bak");
          isibak();
          xSemaphoreGive(relayMutex);
          vTaskDelay(10 * 60 * 1000 / portTICK_PERIOD_MS); // 10 menit untuk isi bak
          
          if (xSemaphoreTake(relayMutex, portMAX_DELAY) == pdTRUE) {
            Serial.println("Auto Sequence: Mixing");
            mixing();
            xSemaphoreGive(relayMutex);
            vTaskDelay(5 * 60 * 1000 / portTICK_PERIOD_MS); // 5 menit untuk mixing
            
            if (xSemaphoreTake(relayMutex, portMAX_DELAY) == pdTRUE) {
              Serial.println("Auto Sequence: Supply");
              supply();
              xSemaphoreGive(relayMutex);
              vTaskDelay(30 * 60 * 1000 / portTICK_PERIOD_MS); // 30 menit untuk supply
              
              if (xSemaphoreTake(relayMutex, portMAX_DELAY) == pdTRUE) {
                Serial.println("Auto Sequence: Off");
                relayoff();
                xSemaphoreGive(relayMutex);
              }
            }
          }
        }
      }
    }
    
    // Delay sebelum iterasi berikutnya
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// Task untuk setup WiFi
void TaskWiFiSetup(void *pvParameters) {
  (void) pvParameters;
  
  // Setup WiFi
  WiFiManager wm;
  bool res = wm.autoConnect("Silagung", "admin123");
  
  if (res) {
    Serial.println("WiFi connected successfully");
  } else {
    Serial.println("Failed to connect to WiFi, operating in offline mode");
  }
  
  // Task ini hanya perlu dijalankan sekali untuk setup WiFi
  vTaskDelete(NULL);
}

// Task untuk membaca perintah serial
void TaskSerialCommand(void *pvParameters) {
  (void) pvParameters;
  
  for (;;) {
    if (Serial.available() > 0) {
      String command = Serial.readStringUntil('\n');
      command.trim();
      
      if (command.equalsIgnoreCase("isibak")) {
        setOperationMode(MODE_ISIBAK);
      } 
      else if (command.equalsIgnoreCase("mixing")) {
        setOperationMode(MODE_MIXING);
      } 
      else if (command.equalsIgnoreCase("supply")) {
        setOperationMode(MODE_SUPPLY);
      } 
      else if (command.equalsIgnoreCase("off")) {
        setOperationMode(MODE_OFF);
      } 
      else if (command.equalsIgnoreCase("auto")) {
        setOperationMode(MODE_AUTO);
      } 
      else if (command.equalsIgnoreCase("status")) {
        DateTime now = rtc.now();
        Serial.print("Current mode: ");
        Serial.println(getModeName(currentMode));
        Serial.print("Waktu: ");
        Serial.println(formatDateTime(now));
        Serial.print("Hari: ");
        Serial.println(daysOfTheWeek[now.dayOfTheWeek()]);
      }
      else if (command.startsWith("settime")) {
        // Format: settime YYYY MM DD HH MM SS
        // Contoh: settime 2023 11 15 14 30 00
        int year, month, day, hour, minute, second;
        if (sscanf(command.c_str(), "settime %d %d %d %d %d %d", &year, &month, &day, &hour, &minute, &second) == 6) {
          rtc.adjust(DateTime(year, month, day, hour, minute, second));
          Serial.println("Waktu RTC telah diatur ulang!");
        } else {
          Serial.println("Format tidak valid. Gunakan: settime YYYY MM DD HH MM SS");
        }
      }
      else if (command.equalsIgnoreCase("disconnect")) {
        WiFi.disconnect(true);
        Serial.println("WiFi disconnected. Please reconnect to configure new network.");
        ESP.restart();
      }
      else {
        Serial.println("Unknown command. Available commands: isibak, mixing, supply, off, auto, status, settime, disconnect");
      }
    }
    
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// Task untuk monitoring RTC
void TaskRTCMonitor(void *pvParameters) {
  (void) pvParameters;
  TickType_t xLastWakeTime;
  const TickType_t xFrequency = 5000 / portTICK_PERIOD_MS; // 5 detik
  
  // Inisialisasi xLastWakeTime dengan waktu saat ini
  xLastWakeTime = xTaskGetTickCount();
  
  for (;;) {
    // Tunggu untuk interval yang tepat
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
    
    // Ambil waktu saat ini dari RTC
    DateTime now = rtc.now();
    
    // Tampilkan informasi waktu
    Serial.print("Waktu: ");
    Serial.println(formatDateTime(now));
    Serial.print("Hari: ");
    Serial.println(daysOfTheWeek[now.dayOfTheWeek()]);
    Serial.print("Mode saat ini: ");
    Serial.println(getModeName(currentMode));
  }
}

// Fungsi untuk mengubah mode operasi
void setOperationMode(OperationMode mode) {
  currentMode = mode;
  Serial.print("Mode changed to: ");
  Serial.println(getModeName(mode));
}

// Mendapatkan nama mode sebagai string
const char* getModeName(OperationMode mode) {
  switch (mode) {
    case MODE_ISIBAK:
      return "Isi Bak";
    case MODE_MIXING:
      return "Mixing";
    case MODE_SUPPLY:
      return "Supply";
    case MODE_OFF:
      return "Off";
    case MODE_AUTO:
      return "Auto";
    default:
      return "Unknown";
  }
}

// Fungsi untuk memeriksa apakah sekarang adalah waktu terjadwal (7 pagi atau 4 sore)
bool isScheduledTime() {
  static bool sequenceStarted = false;
  static unsigned long lastCheckTime = 0;
  
  // Hanya periksa setiap menit untuk menghemat sumber daya
  if (millis() - lastCheckTime < 60000) {
    return sequenceStarted;
  }
  
  lastCheckTime = millis();
  DateTime now = rtc.now();
  int currentHour = now.hour();
  int currentMinute = now.minute();
  
  // Jam 7 pagi (7:00) atau jam 4 sore (16:00) tepat
  bool isScheduledHour = (currentHour == 7 && currentMinute == 0) || 
                         (currentHour == 16 && currentMinute == 0);
  
  // Jika waktu terjadwal dan sekuens belum dimulai, mulai sekuens
  if (isScheduledHour && !sequenceStarted) {
    sequenceStarted = true;
    return true;
  }
  
  // Jika sekuens sudah dimulai, reset flag setelah 1 menit
  // untuk mencegah sekuens berjalan berulang kali dalam satu jam
  if (sequenceStarted && ((currentHour == 7 && currentMinute > 0) || 
                          (currentHour == 16 && currentMinute > 0))) {
    sequenceStarted = false;
  }
  
  return false;
}

// Fungsi relay yang sudah ada
void isibak(){
  digitalWrite(RELAY1, HIGH);
  digitalWrite(RELAY2, HIGH);
  digitalWrite(RELAY3, LOW);
  digitalWrite(RELAY4, HIGH);
  digitalWrite(RELAY5, LOW);
  digitalWrite(RELAY6, LOW);
}

void mixing(){
  digitalWrite(RELAY1, LOW);
  digitalWrite(RELAY2, LOW);
  digitalWrite(RELAY3, HIGH);
  digitalWrite(RELAY4, HIGH);
  digitalWrite(RELAY5, LOW);
  digitalWrite(RELAY6, LOW);
}

void supply(){
  digitalWrite(RELAY1, LOW);
  digitalWrite(RELAY2, LOW);
  digitalWrite(RELAY3, HIGH);
  digitalWrite(RELAY4, LOW);
  digitalWrite(RELAY5, HIGH);
  digitalWrite(RELAY6, LOW);
}

void relayoff(){
  digitalWrite(RELAY1, LOW);
  digitalWrite(RELAY2, LOW); 
  digitalWrite(RELAY3, LOW);
  digitalWrite(RELAY4, LOW);
  digitalWrite(RELAY5, LOW);
  digitalWrite(RELAY6, LOW);
}
