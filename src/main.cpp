// By Fatih Nurrobi Alansshori Teknik Komputer UPI

#include <Arduino.h>
#include <WiFiManager.h>

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

// Fungsi relay
void isibak();
void mixing();
void supply();
void relayoff();
void setOperationMode(OperationMode mode);
const char* getModeName(OperationMode mode);

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
            Serial.println("Mode: Isi Bak");
            break;
          case MODE_MIXING:
            mixing();
            Serial.println("Mode: Mixing");
            break;
          case MODE_SUPPLY:
            supply();
            Serial.println("Mode: Supply");
            break;
          case MODE_OFF:
            relayoff();
            Serial.println("Mode: Off");
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
      // Ambil mutex sebelum mengakses relay
      if (xSemaphoreTake(relayMutex, portMAX_DELAY) == pdTRUE) {
        // Jalankan sekuens
        Serial.println("Auto Sequence: Isi Bak");
        isibak();
        xSemaphoreGive(relayMutex);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        
        if (xSemaphoreTake(relayMutex, portMAX_DELAY) == pdTRUE) {
          Serial.println("Auto Sequence: Mixing");
          mixing();
          xSemaphoreGive(relayMutex);
          vTaskDelay(2000 / portTICK_PERIOD_MS);
          
          if (xSemaphoreTake(relayMutex, portMAX_DELAY) == pdTRUE) {
            Serial.println("Auto Sequence: Supply");
            supply();
            xSemaphoreGive(relayMutex);
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            
            if (xSemaphoreTake(relayMutex, portMAX_DELAY) == pdTRUE) {
              Serial.println("Auto Sequence: Off");
              relayoff();
              xSemaphoreGive(relayMutex);
              vTaskDelay(2000 / portTICK_PERIOD_MS);
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

// Tambahkan task baru untuk membaca perintah serial
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
        Serial.print("Current mode: ");
        Serial.println(getModeName(currentMode));
      }
      else if (command.equalsIgnoreCase("disconnect")) {
        WiFi.disconnect(true);
        Serial.println("WiFi disconnected. Please reconnect to configure new network.");
        ESP.restart();
      }
      else {
        Serial.println("Unknown command. Available commands: isibak, mixing, supply, off, auto, status");
      }
    }
    
    vTaskDelay(100 / portTICK_PERIOD_MS);
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
