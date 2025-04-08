// By Fatih Nurrobi Alansshori Teknik Komputer UPI

#include <Arduino.h>
#include <WiFiManager.h>
#include <Wire.h>
#include <RTClib.h>
#include <Firebase_ESP_Client.h>
#include <ModbusMaster.h>
#include <HardwareSerial.h>

// Konfigurasi RS485 untuk ESP32-S3 dengan RS485 terintegrasi
#define RS485_RX 18  // Pin RX untuk RS485
#define RS485_TX 17  // Pin TX untuk RS485
#define RS485_DE 16  // Pin DE/RE untuk RS485

// Gunakan HardwareSerial untuk ESP32
HardwareSerial rs485Serial(1); // Gunakan Serial1
ModbusMaster node;  // Buat objek Modbus

// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// Firebase
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// Definisi pin relay
#define RELAY1 1
#define RELAY2 2
#define RELAY3 41
#define RELAY4 42
#define RELAY5 45
#define RELAY6 46

// Tentukan pin I2C secara eksplisit
#define SDA_PIN 8
#define SCL_PIN 9

// Firebase credentials
#define API_KEY "AIzaSyASs8IMEdH5s-ne-W7zVQ7nY4Bl9VbQgEE"
#define DATABASE_URL "https://silagung-default-rtdb.asia-southeast1.firebasedatabase.app"

// Struktur untuk menyimpan konfigurasi jadwal dan durasi
struct SystemConfig {
  String morningTime = "07:00";
  String eveningTime = "16:00";
  int isibakDuration = 10;
  int mixingDuration = 5;
  int supplyDuration = 30;
};

// Deklarasi variabel konfigurasi sistem
SystemConfig sysConfig;

// Inisialisasi objek Firebase
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;

// Inisialisasi objek RTC
RTC_DS3231 rtc;
char daysOfTheWeek[7][12] = {"Minggu", "Senin", "Selasa", "Rabu", "Kamis", "Jumat", "Sabtu"};

// Mode operasi
enum OperationMode {
  MODE_ISIBAK,
  MODE_MIXING,
  MODE_SUPPLY,
  MODE_OFF,
  MODE_AUTO
};

// Variabel untuk mode operasi saat ini
OperationMode currentMode = MODE_OFF;

// Variabel global untuk menyimpan mode yang akan diupdate ke Firebase
OperationMode pendingModeUpdate = MODE_OFF;
bool modeUpdatePending = false;

// Mutex untuk akses ke relay dan Firebase
SemaphoreHandle_t relayMutex;
SemaphoreHandle_t firebaseMutex;

// Deklarasi task
void TaskLocalOperation(void *pvParameters);
void TaskAutoSequence(void *pvParameters);
void TaskWiFiSetup(void *pvParameters);
void TaskRTCMonitor(void *pvParameters);
void TaskFirebaseMonitor(void *pvParameters);

// Fungsi relay
void isibak();
void mixing();
void supply();
void relayoff();
void setOperationMode(OperationMode mode);
const char* getModeName(OperationMode mode);
bool isScheduledTime();
void updateFirebaseStatus();
void printStatus();

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
  firebaseMutex = xSemaphoreCreateMutex();

  // Inisialisasi pin relay
  pinMode(RELAY1, OUTPUT);  
  pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT);
  pinMode(RELAY4, OUTPUT);
  pinMode(RELAY5, OUTPUT);
  pinMode(RELAY6, OUTPUT);

  // Matikan semua relay pada awalnya
  relayoff();

  // Inisialisasi I2C dengan pin yang ditentukan
  Wire.begin(SDA_PIN, SCL_PIN);

  // Inisialisasi RTC
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  }

  // Cek apakah RTC kehilangan daya
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting default time!");
    // Atur waktu default jika RTC kehilangan daya
    rtc.adjust(DateTime(2025, 3, 25, 12, 1, 0));
  }

  // Tampilkan waktu saat ini
  DateTime now = rtc.now();
  Serial.print("Waktu saat ini: ");
  Serial.println(formatDateTime(now));
  Serial.print("Hari: ");
  Serial.println(daysOfTheWeek[now.dayOfTheWeek()]);
  Serial.print("Suhu: ");
  Serial.print(rtc.getTemperature());
  Serial.println(" C");

  // Buat task untuk operasi lokal
  xTaskCreate(
    TaskLocalOperation,     // Fungsi task
    "LocalOperation",       // Nama task
    2048,                   // Stack size
    NULL,                   // Parameter
    1,                      // Prioritas
    NULL                    // Task handle
  );

  // Buat task untuk sekuens otomatis
  xTaskCreate(
    TaskAutoSequence,       // Fungsi task
    "AutoSequence",         // Nama task
    2048,                   // Stack size
    NULL,                   // Parameter
    1,                      // Prioritas
    NULL                    // Task handle
  );

  // Buat task untuk setup WiFi
  xTaskCreate(
    TaskWiFiSetup,          // Fungsi task
    "WiFiSetup",            // Nama task
    8192,                   // Stack size (ditingkatkan)
    NULL,                   // Parameter
    2,                      // Prioritas (lebih tinggi untuk setup WiFi)
    NULL                    // Task handle
  );

  // Buat task untuk monitoring RTC
  xTaskCreate(
    TaskRTCMonitor,         // Fungsi task
    "RTCMonitor",           // Nama task
    2048,                   // Stack size
    NULL,                   // Parameter
    1,                      // Prioritas
    NULL                    // Task handle
  );
  
  // Tambahkan task untuk monitoring Firebase dengan stack size yang lebih besar
  xTaskCreatePinnedToCore(
    TaskFirebaseMonitor,    // Fungsi task
    "FirebaseMonitor",      // Nama task
    32768,                  // Stack size (diperbesar)
    NULL,                   // Parameter
    1,                      // Prioritas
    NULL,                   // Task handle
    1                       // Core yang digunakan (pin ke core 1)
  );
}

void loop() {
  // Tidak ada yang perlu dilakukan di sini karena semua operasi ditangani oleh task
  // Kode untuk kontrol Modbus VFD ATV12
  static bool vfdEnabled = false;
  static float vfdFrequency = 0;
  static unsigned long lastModbusRead = 0;
  
  // Inisialisasi Modbus jika belum diinisialisasi
  static bool modbusInitialized = false;
  if (!modbusInitialized) {
    // Inisialisasi RS485 dengan HardwareSerial
    Serial1.begin(9600, SERIAL_8N1, 18, 17); // RX=18, TX=17
    
    pinMode(16, OUTPUT); // DE/RE pin
    digitalWrite(16, LOW);
    
    node.begin(1, Serial1);  // Slave ID = 1
    node.preTransmission([]() {
      digitalWrite(16, HIGH);
      delay(2);
    });
    node.postTransmission([]() {
      digitalWrite(16, LOW);
    });
    
    modbusInitialized = true;
    Serial.println("Modbus untuk ATV12 VFD diinisialisasi");
  }
  
  // Cek apakah mode saat ini memerlukan VFD aktif
  bool shouldVfdBeActive = (currentMode == MODE_MIXING || 
                           currentMode == MODE_SUPPLY ||
                           currentMode == MODE_ISIBAK)
                           ;
  // Cek jika mode OFF, atur frekuensi VFD menjadi 0
  if (currentMode == MODE_OFF) {
    vfdFrequency = 0.0;
    Serial.println("Mode OFF: Mengatur frekuensi VFD menjadi 0 Hz");
  } else {
    // Jika dalam mode MIXING, SUPPLY, ISIBAK, atau AUTO, gunakan frekuensi 48 Hz
    vfdFrequency = 48.0;
    Serial.println("Mode aktif: Mengatur frekuensi VFD menjadi 48 Hz");
  }
  // Aktifkan atau nonaktifkan VFD sesuai mode
  if (shouldVfdBeActive && !vfdEnabled) {
    // Aktifkan VFD dengan frekuensi yang ditentukan
    Serial.println("Mengaktifkan VFD pada " + String(vfdFrequency) + " Hz");
    
    // Start sequence dengan arah forward
    uint8_t result;
    result = node.writeSingleRegister(8501, 6);  // Pre-charge
    delay(100);
    result = node.writeSingleRegister(8501, 7);  // Start
    delay(100);
    
    // Set frekuensi (48Hz = 480 dalam format VFD)
    uint16_t freqValue = (uint16_t)(vfdFrequency * 10);
    result = node.writeSingleRegister(8502, freqValue);
    delay(100);
    
    result = node.writeSingleRegister(8501, 15);  // Forward run
    
    if (result == node.ku8MBSuccess) {
      Serial.println("VFD berhasil diaktifkan pada " + String(vfdFrequency) + " Hz");
      vfdEnabled = true;
    } else {
      Serial.println("Gagal mengaktifkan VFD!");
    }
  } else if (!shouldVfdBeActive && vfdEnabled) {
    // Nonaktifkan VFD
    Serial.println("Menonaktifkan VFD");
    uint8_t result = node.writeSingleRegister(8501, 0);
    if (result == node.ku8MBSuccess) {
      Serial.println("VFD berhasil dinonaktifkan");
      vfdEnabled = false;
    } else {
      Serial.println("Gagal menonaktifkan VFD!");
    }
  }
  
  // Baca frekuensi aktual setiap 5 detik
  if (vfdEnabled && millis() - lastModbusRead >= 5000) {
    uint8_t result = node.readHoldingRegisters(8503, 1);
    if (result == node.ku8MBSuccess) {
      int16_t freq = node.getResponseBuffer(0);
      float actualFreq = freq / 10.0;
      Serial.print("Frekuensi VFD aktual: ");
      Serial.print(actualFreq);
      Serial.println(" Hz");
    }
    lastModbusRead = millis();
  }
  vTaskDelay(1000 / portTICK_PERIOD_MS);
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
    if (currentMode == MODE_AUTO) {
      if (isScheduledTime()) {
        Serial.println("Waktu terjadwal terdeteksi, memulai sekuens otomatis");
        
        if (xSemaphoreTake(relayMutex, portMAX_DELAY) == pdTRUE) {
          Serial.println("Auto Sequence: Isi Bak");
          isibak();
          xSemaphoreGive(relayMutex);
          vTaskDelay(sysConfig.isibakDuration * 60 * 1000 / portTICK_PERIOD_MS);
          
          if (xSemaphoreTake(relayMutex, portMAX_DELAY) == pdTRUE) {
            Serial.println("Auto Sequence: Mixing");
            mixing();
            xSemaphoreGive(relayMutex);
            vTaskDelay(sysConfig.mixingDuration * 60 * 1000 / portTICK_PERIOD_MS);
            
            if (xSemaphoreTake(relayMutex, portMAX_DELAY) == pdTRUE) {
              Serial.println("Auto Sequence: Supply");
              supply();
              xSemaphoreGive(relayMutex);
              vTaskDelay(sysConfig.supplyDuration * 60 * 1000 / portTICK_PERIOD_MS);
              
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
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// Task untuk setup WiFi dan Firebase
void TaskWiFiSetup(void *pvParameters) {
  (void) pvParameters;
  
  // Inisialisasi WiFiManager
  WiFiManager wm;
  
  // Atur timeout
  wm.setConfigPortalTimeout(180);
  
  // Atur nama AP dan password
  bool res = wm.autoConnect("Silagung", "admin123");
  
  if (res) {
    Serial.println("WiFi connected successfully");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    
    // Tunggu sebentar sebelum inisialisasi Firebase
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    
    // Inisialisasi Firebase dengan stack yang cukup
    if (xSemaphoreTake(firebaseMutex, portMAX_DELAY) == pdTRUE) {
      Serial.println("Initializing Firebase...");
      
      // Konfigurasi Firebase
      config.api_key = API_KEY;
      config.database_url = DATABASE_URL;
      
      Serial.println("Firebase API Key: " + String(API_KEY));
      Serial.println("Firebase Database URL: " + String(DATABASE_URL));
      
      // Autentikasi anonim
      Serial.println("Attempting Firebase signup...");
      if (Firebase.signUp(&config, &auth, "", "")) {
        Serial.println("Firebase signup OK");
        signupOK = true;
      } else {
        Serial.println("Firebase signup failed");
        if (config.signer.signupError.message.length() > 0) {
          Serial.print("Reason: ");
          Serial.println(config.signer.signupError.message.c_str());
        }
      }
      
      // Callback untuk token
      config.token_status_callback = tokenStatusCallback;
      
      // Mulai koneksi Firebase
      Serial.println("Starting Firebase connection...");
      Firebase.begin(&config, &auth);
      Firebase.reconnectWiFi(true);
      
      Serial.println("Firebase setup complete");
      xSemaphoreGive(firebaseMutex);
    }
  } else {
    Serial.println("WiFi connection failed");
  }
  
  // Task selesai, hapus diri sendiri
  vTaskDelete(NULL);
}

// Task untuk monitoring RTC
void TaskRTCMonitor(void *pvParameters) {
  (void) pvParameters;
  
  for (;;) {
    // Tampilkan waktu setiap menit
    static int lastMinute = -1;
    DateTime now = rtc.now();
    
    if (now.minute() != lastMinute) {
      lastMinute = now.minute();
      
      // Tampilkan waktu saat ini
      Serial.print("Waktu: ");
      Serial.println(formatDateTime(now));
      Serial.print("Hari: ");
      Serial.println(daysOfTheWeek[now.dayOfTheWeek()]);
      Serial.print("Suhu: ");
      Serial.print(rtc.getTemperature());
      Serial.println(" C");
      Serial.print("Mode saat ini: ");
      Serial.println(getModeName(currentMode));
    }
    
    // Delay sebelum iterasi berikutnya
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// Task untuk monitoring Firebase
void TaskFirebaseMonitor(void *pvParameters) {
  (void) pvParameters;
  
  // Tunggu sampai WiFi dan Firebase siap
  vTaskDelay(10000 / portTICK_PERIOD_MS);
  
  Serial.println("Starting Firebase monitor task");
  
  for (;;) {
    if (WiFi.status() == WL_CONNECTED && signupOK) {
      // Periksa konfigurasi setiap 5 menit
      static unsigned long lastConfigCheck = 0;
      if (millis() - lastConfigCheck > 300000) {
        if (xSemaphoreTake(firebaseMutex, portMAX_DELAY) == pdTRUE) {
          // Baca konfigurasi jadwal
          if (Firebase.RTDB.getString(&fbdo, "silagung-controller/config/schedule/morning")) {
            sysConfig.morningTime = fbdo.stringData();
          }
          if (Firebase.RTDB.getString(&fbdo, "silagung-controller/config/schedule/evening")) {
            sysConfig.eveningTime = fbdo.stringData();
          }
          
          // Baca konfigurasi durasi
          if (Firebase.RTDB.getInt(&fbdo, "silagung-controller/config/duration/isibak")) {
            sysConfig.isibakDuration = fbdo.intData();
          }
          if (Firebase.RTDB.getInt(&fbdo, "silagung-controller/config/duration/mixing")) {
            sysConfig.mixingDuration = fbdo.intData();
          }
          if (Firebase.RTDB.getInt(&fbdo, "silagung-controller/config/duration/supply")) {
            sysConfig.supplyDuration = fbdo.intData();
          }
          
          xSemaphoreGive(firebaseMutex);
        }
        lastConfigCheck = millis();
      }
      
      // Periksa perintah dari Firebase dengan timeout
      if (xSemaphoreTake(firebaseMutex, portMAX_DELAY) == pdTRUE) {
        Firebase.RTDB.setReadTimeout(&fbdo, 1000);
        if (Firebase.RTDB.getString(&fbdo, "silagung-controller/commands/setMode")) {
          String command = fbdo.stringData();
          
          if (command.length() > 0) {
            Serial.print("Menerima perintah: ");
            Serial.println(command);
            
            // Eksekusi perintah
            if (command == "isibak") {
              setOperationMode(MODE_ISIBAK);
            } else if (command == "mixing") {
              setOperationMode(MODE_MIXING);
            } else if (command == "supply") {
              setOperationMode(MODE_SUPPLY);
            } else if (command == "off") {
              setOperationMode(MODE_OFF);
            } else if (command == "auto") {
              setOperationMode(MODE_AUTO);
            }
            
            // Reset perintah
            vTaskDelay(100 / portTICK_PERIOD_MS);
            Firebase.RTDB.setString(&fbdo, "silagung-controller/commands/setMode", "");
          }
        }
        
        xSemaphoreGive(firebaseMutex);
      }
      
      // Update status ke Firebase setiap 30 detik
      static unsigned long lastUpdateTime = 0;
      if (millis() - lastUpdateTime > 30000) {
        updateFirebaseStatus();
        lastUpdateTime = millis();
      }
    }
    
    // Berikan waktu untuk task lain
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// Fungsi untuk memeriksa apakah sekarang adalah waktu terjadwal
bool isScheduledTime() {
  static bool sequenceStarted = false;
  static unsigned long lastCheckTime = 0;
  
  if (millis() - lastCheckTime < 100) {
    return sequenceStarted;
  }
  
  lastCheckTime = millis();
  DateTime now = rtc.now();
  
  char currentTime[6];
  sprintf(currentTime, "%02d:%02d", now.hour(), now.minute());
  String timeNow = String(currentTime);
  
  bool isScheduledHour = (timeNow == sysConfig.morningTime || timeNow == sysConfig.eveningTime) &&
                        now.second() < 60;
  
  if (isScheduledHour && !sequenceStarted) {
    Serial.println("Waktu terjadwal terdeteksi: " + timeNow);
    sequenceStarted = true;
    return true;
  }
  
  if (sequenceStarted && timeNow != sysConfig.morningTime && timeNow != sysConfig.eveningTime) {
    sequenceStarted = false;
  }
  
  return sequenceStarted;
}

// Fungsi untuk update status ke Firebase
void updateFirebaseStatus() {
  if (WiFi.status() == WL_CONNECTED && signupOK) {
    if (xSemaphoreTake(firebaseMutex, portMAX_DELAY) == pdTRUE) {
      DateTime now = rtc.now();
      bool updateSuccess = true;
      
      // Update waktu
      if (Firebase.RTDB.setString(&fbdo, "silagung-controller/status/time", formatDateTime(now))) {
        Serial.println("Time update successful");
      } else {
        Serial.print("Time update failed: ");
        Serial.println(fbdo.errorReason());
        updateSuccess = false;
      }
      
      // Jika update pertama gagal, jangan lanjutkan
      if (updateSuccess) {
        // Update hari
        if (Firebase.RTDB.setString(&fbdo, "silagung-controller/status/day", daysOfTheWeek[now.dayOfTheWeek()])) {
          Serial.println("Day update successful");
        } else {
          Serial.print("Day update failed: ");
          Serial.println(fbdo.errorReason());
        }
        
        // Update suhu
        if (Firebase.RTDB.setFloat(&fbdo, "silagung-controller/status/temperature", rtc.getTemperature())) {
          Serial.println("Temperature update successful");
        } else {
          Serial.print("Temperature update failed: ");
          Serial.println(fbdo.errorReason());
        }
        
        // Update mode
        if (Firebase.RTDB.setString(&fbdo, "silagung-controller/status/currentMode", getModeName(currentMode))) {
          Serial.println("Mode update successful");
        } else {
          Serial.print("Mode update failed: ");
          Serial.println(fbdo.errorReason());
        }
        
        // Update timestamp
        if (Firebase.RTDB.setInt(&fbdo, "silagung-controller/status/lastUpdate", now.unixtime())) {
          Serial.println("Timestamp update successful");
        } else {
          Serial.print("Timestamp update failed: ");
          Serial.println(fbdo.errorReason());
        }
      }
      
      xSemaphoreGive(firebaseMutex);
    }
  }
}

// Fungsi untuk mengubah mode operasi - dioptimalkan untuk mengurangi penggunaan stack
void setOperationMode(OperationMode mode) {
  // Simpan mode baru
  currentMode = mode;
  
  // Log perubahan mode
  Serial.print("Mode changed to: ");
  Serial.println(getModeName(mode));
  
  // Jika beralih ke mode AUTO dan bukan waktu terjadwal, matikan semua relay
  if (mode == MODE_AUTO && !isScheduledTime()) {
    if (xSemaphoreTake(relayMutex, portMAX_DELAY) == pdTRUE) {
      relayoff();
      xSemaphoreGive(relayMutex);
    }
  }
  
  // Tandai bahwa ada update mode yang pending untuk Firebase
  pendingModeUpdate = mode;
  modeUpdatePending = true;
}

// Fungsi untuk mendapatkan nama mode
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

// Fungsi terpisah untuk menampilkan status - mengurangi penggunaan stack
void printStatus() {
  DateTime now = rtc.now();
  Serial.print("Mode saat ini: ");
  Serial.println(getModeName(currentMode));
  Serial.print("Waktu: ");
  Serial.println(formatDateTime(now));
  Serial.print("Hari: ");
  Serial.println(daysOfTheWeek[now.dayOfTheWeek()]);
  Serial.print("Suhu: ");
  Serial.print(rtc.getTemperature());
  Serial.println(" C");
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
  digitalWrite(RELAY1, HIGH);
  digitalWrite(RELAY2, LOW);
  digitalWrite(RELAY3, LOW);
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