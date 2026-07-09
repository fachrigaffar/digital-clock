#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <Preferences.h>
#include <time.h>
#include <esp_sntp.h>

// ==========================================
// 1. PIN DEFINITIONS & CONFIGURATION
// ==========================================
#define PIN_MQ_SENSOR 26   // Gas/Smoke Analog Input (A0)
#define PIN_BUTTON_1 14    // Push Button 1 (PB1) - Back / Cancel / Silence
#define PIN_BUTTON_2 12    // Push Button 2 (PB2) - Menu / Next / Confirm / Hapus
#define PIN_BUTTON_3 13    // Push Button 3 (PB3) - Select / Increment / Edit
#define PIN_BUZZER 27      // Active Buzzer (BZ)
#define PIN_OLED_SDA 21    // OLED SDA Pin (D21)
#define PIN_OLED_SCL 22    // OLED SCL Pin (D22)

// OLED Display Configuration - modul 0.91" umumnya 128x32
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

// System Constants
#define GAS_ALARM_THRESHOLD 400
#define HEARTBEAT_INTERVAL_MS 30000
#define BUTTON_DEBOUNCE_MS 50

// Pomodoro Constants
#define POMODORO_MIN_MINUTES 5
#define POMODORO_MAX_MINUTES 60
#define POMODORO_STEP_MINUTES 5
#define POMODORO_DEFAULT_MINUTES 25

// Menu Constants
#define MENU_ITEM_COUNT 4

// ==========================================
// 2. GLOBAL VARIABLES
// ==========================================
enum SystemMode {
  MODE_CLOCK,
  MODE_MENU,
  MODE_ALARM_ACTION,     // pilihan Edit / Hapus / Batal saat alarm sudah ada
  MODE_SET_ALARM,        // satu layar untuk jam & menit sekaligus
  MODE_POMODORO_SET,
  MODE_POMODORO_RUNNING,
  MODE_POMODORO_DONE,
  MODE_SET_MQTT_HOST,    // edit IP broker MQTT per-oktet
  MODE_SET_MQTT_PORT,    // pilih 1883 / 8883
  MODE_WIFI_RESET_CONFIRM
};
SystemMode currentMode = MODE_CLOCK;

// Temp values saat edit alarm
int tempHour = 0;
int tempMinute = 0;
int alarmEditField = 0; // 0 = jam sedang diedit, 1 = menit sedang diedit

// Menu utama
const char* menuItems[MENU_ITEM_COUNT] = { "Set Alarm", "Pomodoro", "Set MQTT", "Reset WiFi" };
int menuIndex = 0;

// Pomodoro
int pomodoroMinutes = POMODORO_DEFAULT_MINUTES;
unsigned long pomodoroEndMillis = 0;
bool isPomodoroRinging = false;

// Temp values saat edit MQTT host/port
int mqttEditOctets[4] = { 192, 168, 1, 1 };
int mqttOctetIndex = 0;
bool mqttUseTLS = false; // false = port 1883, true = port 8883

// Alarm variables
bool isAlarmActive = false;
String alarmTimeStr = "";
bool manualAlarmSilence = false;
bool wifiResetRequested = false;

// Debounce
int lastBtn1State = HIGH;
int lastBtn2State = HIGH;
int lastBtn3State = HIGH;
unsigned long lastBtn1Time = 0;
unsigned long lastBtn2Time = 0;
unsigned long lastBtn3Time = 0;

// WiFiManager Custom Parameters
char mqtt_host[80] = "";
char mqtt_port[6] = "8883";
char mqtt_user[40] = "";
char mqtt_pass[40] = "";
char device_id[40] = "esp32-clock-01";

// MQTT
WiFiClientSecure secureClient;
WiFiClient normalClient;
PubSubClient* mqttClient = nullptr;

unsigned long lastReconnectAttempt = 0;
unsigned long lastHeartbeat = 0;
bool lastGasLeakState = false;
bool lastAlarmActiveState = false;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Preferences preferences;

// Forward declarations
void setBuzzerState(bool active);
void setAlarmTime(String timeStr);
void publishStatus(int gasLevel, bool alarmActive);
bool checkAndSilenceAlarm();
void selectMenuItem(int idx);
void startPomodoro();
void stopPomodoro();
void stopPomodoroRinging();
void resetWiFiSettings();
void clearAlarm();
void initMQTT();
void loadOctetsFromHost();
void saveMqttConfig();
void applyMqttConfig();

// ==========================================
// 3. OLED DISPLAY MODULE (dioptimalkan untuk 128x32)
// ==========================================
void initDisplay() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Smart Clock"));
  display.println(F("Initializing..."));
  display.display();
  delay(800);
}

void showSetupScreen(const char* apName) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Setup WiFi, hubungkan:"));
  display.println(apName);
  display.println(F("Buka: 192.168.4.1"));
  display.display();
}

void showStatusScreen(const char* title, const char* message) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(title);
  display.println(message);
  display.display();
}

void showClockScreen(const char* timeStr, const char* dateStr, int gasLevel, bool alarmSet, const char* alarmTime, bool wifiConnected, bool mqttConnected) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(wifiConnected ? F("W:V") : F("W:X"));
  display.print(F(" "));
  display.print(mqttConnected ? F("M:V") : F("M:X"));
  if (alarmSet) {
    display.print(F(" AL:"));
    display.print(alarmTime);
  }

  display.setTextSize(2);
  display.setCursor(28, 8);
  display.print(timeStr);

  display.setTextSize(1);
  display.setCursor(0, 24);
  display.print(F("Gas:"));
  display.print(gasLevel);
  display.print(F(" Aman | "));
  display.print(dateStr);

  display.display();
}

void showAlarmRingingScreen(const char* timeStr) {
  bool blink = (millis() / 400) % 2;

  display.clearDisplay();
  if (blink) {
    display.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else {
    display.setTextColor(SSD1306_WHITE);
  }

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("=== ALARM! ==="));

  display.setTextSize(2);
  display.setCursor(28, 8);
  display.print(timeStr);

  display.setTextSize(1);
  display.setCursor(0, 24);
  display.print(F("PB1: Matikan"));

  display.display();
  display.setTextColor(SSD1306_WHITE);
}

void showGasAlertScreen(int gasLevel) {
  bool blink = (millis() / 250) % 2;

  display.clearDisplay();
  if (blink) {
    display.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else {
    display.setTextColor(SSD1306_WHITE);
  }

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("!! BAHAYA GAS !!"));

  display.setTextSize(2);
  display.setCursor(0, 8);
  display.print(F("Gas:"));
  display.print(gasLevel);

  display.setTextSize(1);
  display.setCursor(0, 24);
  display.print(F("Cek sumber gas!"));

  display.display();
  display.setTextColor(SSD1306_WHITE);
}

void showMenuScreen(int selectedIndex) {
  display.clearDisplay();
  display.setTextSize(1);

  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    int y = i * 8;
    if (i == selectedIndex) {
      display.fillRect(0, y, SCREEN_WIDTH, 8, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(2, y);
    display.print(i + 1);
    display.print(F(". "));
    display.print(menuItems[i]);
  }

  // Menu 4 item pas mengisi 32px (4 x 8px), jadi hint tombol digabung di judul saja
  display.setTextColor(SSD1306_WHITE);
  display.display();
}

// Layar aksi saat alarm sudah ada isinya: Edit / Hapus / Batal
void showAlarmActionScreen(const char* alarmTime) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("Alarm aktif: "));
  display.print(alarmTime);

  display.setTextSize(1);
  display.setCursor(0, 12);
  display.print(F("PB3:Edit  PB2:Hapus"));
  display.setCursor(0, 24);
  display.print(F("PB1:Batal"));

  display.display();
}

void showSetAlarmScreen(int hour, int minute, int editField) {
  char hh[3], mm[3];
  sprintf(hh, "%02d", hour);
  sprintf(mm, "%02d", minute);

  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("Atur Alarm"));

  display.setTextSize(2);
  display.setCursor(34, 8);

  if (editField == 0) display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  else display.setTextColor(SSD1306_WHITE);
  display.print(hh);

  display.setTextColor(SSD1306_WHITE);
  display.print(":");

  if (editField == 1) display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  else display.setTextColor(SSD1306_WHITE);
  display.print(mm);

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 24);
  display.print(F("PB3:+1 PB2:"));
  display.print(editField == 0 ? F("Lanjut") : F("Simpan"));

  display.display();
}

// Edit IP broker MQTT per-oktet, oktet aktif disorot kotak putih
void showSetMqttHostScreen(int oct[4], int editIndex) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("Set MQTT Host"));

  int xPos[4] = { 0, 26, 52, 78 };
  for (int i = 0; i < 4; i++) {
    char buf[4];
    sprintf(buf, "%03d", oct[i]);

    if (i == editIndex) {
      display.fillRect(xPos[i], 10, 20, 10, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(xPos[i] + 1, 11);
    display.print(buf);

    display.setTextColor(SSD1306_WHITE);
    if (i < 3) {
      display.setCursor(xPos[i] + 21, 11);
      display.print(".");
    }
  }

  display.setCursor(0, 24);
  display.print(F("PB3:+1 PB2:"));
  display.print(editIndex == 3 ? F("Lanjut") : F("Next"));
  display.display();
}

// Pilih port: 1883 (tanpa TLS) atau 8883 (dengan TLS)
void showSetMqttPortScreen(bool useTLS) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("Set MQTT Port"));

  display.setTextSize(2);
  display.setCursor(28, 8);
  display.print(useTLS ? F("8883") : F("1883"));

  display.setTextSize(1);
  display.setCursor(0, 24);
  display.print(F("PB3:Ganti PB2:Simpan"));
  display.display();
}

void showPomodoroSetScreen(int minutes) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("Set Pomodoro"));

  display.setTextSize(2);
  display.setCursor(36, 8);
  display.print(minutes);
  display.print(F(" mnt"));

  display.setTextSize(1);
  display.setCursor(0, 24);
  display.print(F("PB3:+5 PB2:Mulai"));
  display.display();
}

void showPomodoroRunningScreen(int remainingSeconds) {
  int mm = remainingSeconds / 60;
  int ss = remainingSeconds % 60;

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("Pomodoro Fokus"));

  display.setTextSize(2);
  display.setCursor(34, 8);
  if (mm < 10) display.print("0");
  display.print(mm);
  display.print(":");
  if (ss < 10) display.print("0");
  display.print(ss);

  display.setTextSize(1);
  display.setCursor(0, 24);
  display.print(F("PB1: Stop"));
  display.display();
}

void showPomodoroDoneScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("Pomodoro"));

  display.setTextSize(2);
  display.setCursor(22, 8);
  display.print(F("SELESAI"));

  display.setTextSize(1);
  display.setCursor(0, 24);
  display.print(F("Tekan tombol utk OK"));
  display.display();
}

void showWifiResetConfirmScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Reset WiFi?"));
  display.println(F("Hapus WiFi&MQTT,"));
  display.println(F("lalu restart."));
  display.print(F("PB3:Ya  PB1:Tidak"));
  display.display();
}

// ==========================================
// 4. GAS SENSOR MODULE
// ==========================================
void initGasSensor() {
  pinMode(PIN_MQ_SENSOR, INPUT);
}

int readGasLevel() {
  return analogRead(PIN_MQ_SENSOR);
}

bool isGasLeaking() {
  return readGasLevel() > GAS_ALARM_THRESHOLD;
}

// ==========================================
// 5. ALARM, POMODORO, MQTT-SETTING & BUTTON MODULE
// ==========================================
void initAlarmAndButtons() {
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_BUTTON_1, INPUT_PULLUP);
  pinMode(PIN_BUTTON_2, INPUT_PULLUP);
  pinMode(PIN_BUTTON_3, INPUT_PULLUP);
}

void setBuzzerState(bool active) {
  digitalWrite(PIN_BUZZER, active ? HIGH : LOW);
}

void setAlarmTime(String timeStr) {
  alarmTimeStr = timeStr;
  manualAlarmSilence = false;
}

// Menghapus alarm sepenuhnya (dipanggil dari layar Alarm Action)
void clearAlarm() {
  alarmTimeStr = "";
  manualAlarmSilence = false;
  isAlarmActive = false;
  setBuzzerState(false);
  publishStatus(readGasLevel(), false);
  Serial.println("Alarm dihapus.");
}

void checkAlarmTrigger(int currentHour, int currentMinute) {
  if (alarmTimeStr == "") return;

  int colonIndex = alarmTimeStr.indexOf(':');
  if (colonIndex == -1) return;

  int alarmHour = alarmTimeStr.substring(0, colonIndex).toInt();
  int alarmMinute = alarmTimeStr.substring(colonIndex + 1).toInt();

  if (currentHour == alarmHour && currentMinute == alarmMinute) {
    if (!manualAlarmSilence) {
      isAlarmActive = true;
    }
  } else {
    manualAlarmSilence = false;
    isAlarmActive = false;
  }
}

bool checkAndSilenceAlarm() {
  if (isAlarmActive) {
    Serial.println("Alarm silenced via button press.");
    isAlarmActive = false;
    manualAlarmSilence = true;
    setBuzzerState(false);
    return true;
  }
  return false;
}

// ---- Pomodoro helpers ----
void startPomodoro() {
  pomodoroEndMillis = millis() + (unsigned long)pomodoroMinutes * 60000UL;
  isPomodoroRinging = false;
  currentMode = MODE_POMODORO_RUNNING;
  Serial.print("Pomodoro dimulai: ");
  Serial.print(pomodoroMinutes);
  Serial.println(" menit");
}

void stopPomodoro() {
  pomodoroEndMillis = 0;
  isPomodoroRinging = false;
  setBuzzerState(false);
}

void stopPomodoroRinging() {
  isPomodoroRinging = false;
  setBuzzerState(false);
}

// ---- MQTT setting helpers ----
// Baca mqtt_host saat ini (format IP a.b.c.d) ke array oktet untuk diedit.
// Kalau bukan format IP valid (mis. masih kosong atau berupa domain), default ke 192.168.1.1
void loadOctetsFromHost() {
  int a = 192, b = 168, c = 1, d = 1;
  if (strlen(mqtt_host) > 0) {
    int parsedCount = sscanf(mqtt_host, "%d.%d.%d.%d", &a, &b, &c, &d);
    if (parsedCount != 4) {
      a = 192; b = 168; c = 1; d = 1;
    }
  }
  mqttEditOctets[0] = a;
  mqttEditOctets[1] = b;
  mqttEditOctets[2] = c;
  mqttEditOctets[3] = d;
  mqttOctetIndex = 0;
}

// Membongkar & membuat ulang koneksi MQTT dengan host/port baru tanpa restart device
void applyMqttConfig() {
  if (mqttClient) {
    mqttClient->disconnect();
    delete mqttClient;
    mqttClient = nullptr;
  }
  initMQTT();
  lastReconnectAttempt = 0; // paksa updateMQTT() coba connect di loop berikutnya
}

// Simpan host+port MQTT hasil edit ke Preferences lalu terapkan
void saveMqttConfig() {
  char newHost[16];
  sprintf(newHost, "%d.%d.%d.%d", mqttEditOctets[0], mqttEditOctets[1], mqttEditOctets[2], mqttEditOctets[3]);
  strncpy(mqtt_host, newHost, sizeof(mqtt_host));
  mqtt_host[sizeof(mqtt_host) - 1] = '\0';
  strcpy(mqtt_port, mqttUseTLS ? "8883" : "1883");

  preferences.begin("mqtt-config", false);
  preferences.putString("host", mqtt_host);
  preferences.putString("port", mqtt_port);
  preferences.end();

  Serial.print("MQTT config disimpan: ");
  Serial.print(mqtt_host);
  Serial.print(":");
  Serial.println(mqtt_port);

  applyMqttConfig();
}

// ---- Menu selection ----
void selectMenuItem(int idx) {
  switch (idx) {
    case 0: // Set Alarm
      if (alarmTimeStr != "") {
        currentMode = MODE_ALARM_ACTION;
        Serial.println("Masuk mode Alarm Action");
      } else {
        tempHour = 0;
        tempMinute = 0;
        alarmEditField = 0;
        currentMode = MODE_SET_ALARM;
        Serial.println("Masuk mode Set Alarm (baru)");
      }
      break;

    case 1: // Pomodoro
      currentMode = MODE_POMODORO_SET;
      Serial.println("Masuk mode Set Pomodoro");
      break;

    case 2: // Set MQTT
      loadOctetsFromHost();
      currentMode = MODE_SET_MQTT_HOST;
      Serial.println("Masuk mode Set MQTT");
      break;

    case 3: // Reset WiFi
      currentMode = MODE_WIFI_RESET_CONFIRM;
      Serial.println("Masuk konfirmasi Reset WiFi");
      break;
  }
}

// ---- Button handlers ----
void onButton1Press() {
  if (checkAndSilenceAlarm()) return;

  if (isPomodoroRinging) {
    stopPomodoroRinging();
    currentMode = MODE_CLOCK;
    return;
  }

  switch (currentMode) {
    case MODE_CLOCK:
      Serial.println("PB1: Silencing Alarm/Buzzer (manual override)");
      manualAlarmSilence = true;
      isAlarmActive = false;
      setBuzzerState(false);
      break;

    case MODE_MENU:
      currentMode = MODE_CLOCK;
      break;

    case MODE_ALARM_ACTION:
      currentMode = MODE_CLOCK;
      Serial.println("Alarm action dibatalkan.");
      break;

    case MODE_SET_ALARM:
      currentMode = MODE_CLOCK;
      Serial.println("Alarm setup cancelled.");
      break;

    case MODE_POMODORO_SET:
      currentMode = MODE_CLOCK;
      break;

    case MODE_POMODORO_RUNNING:
      stopPomodoro();
      currentMode = MODE_CLOCK;
      Serial.println("Pomodoro dibatalkan.");
      break;

    case MODE_POMODORO_DONE:
      stopPomodoroRinging();
      currentMode = MODE_CLOCK;
      break;

    case MODE_SET_MQTT_HOST:
    case MODE_SET_MQTT_PORT:
      currentMode = MODE_CLOCK;
      Serial.println("Set MQTT dibatalkan.");
      break;

    case MODE_WIFI_RESET_CONFIRM:
      currentMode = MODE_CLOCK;
      Serial.println("Reset WiFi dibatalkan.");
      break;
  }
}

void onButton2Press() {
  if (checkAndSilenceAlarm()) return;

  if (isPomodoroRinging) {
    stopPomodoroRinging();
    currentMode = MODE_CLOCK;
    return;
  }

  switch (currentMode) {
    case MODE_CLOCK:
      currentMode = MODE_MENU;
      menuIndex = 0;
      Serial.println("Masuk Menu");
      break;

    case MODE_MENU:
      menuIndex = (menuIndex + 1) % MENU_ITEM_COUNT;
      break;

    case MODE_ALARM_ACTION:
      // Hapus alarm
      clearAlarm();
      currentMode = MODE_CLOCK;
      break;

    case MODE_SET_ALARM:
      if (alarmEditField == 0) {
        alarmEditField = 1; // pindah ke edit menit
      } else {
        currentMode = MODE_CLOCK;
        char formattedTime[6];
        sprintf(formattedTime, "%02d:%02d", tempHour, tempMinute);
        setAlarmTime(String(formattedTime));
        publishStatus(readGasLevel(), isAlarmActive);
        Serial.print("Alarm disimpan: ");
        Serial.println(formattedTime);
      }
      break;

    case MODE_POMODORO_SET:
      startPomodoro();
      break;

    case MODE_SET_MQTT_HOST:
      if (mqttOctetIndex < 3) {
        mqttOctetIndex++;
      } else {
        // Sudah selesai isi 4 oktet -> lanjut pilih port, default sesuai port tersimpan
        mqttUseTLS = (atoi(mqtt_port) == 8883);
        currentMode = MODE_SET_MQTT_PORT;
      }
      break;

    case MODE_SET_MQTT_PORT:
      saveMqttConfig();
      currentMode = MODE_CLOCK;
      break;

    default:
      break;
  }
}

void onButton3Press() {
  if (checkAndSilenceAlarm()) return;

  if (isPomodoroRinging) {
    stopPomodoroRinging();
    currentMode = MODE_CLOCK;
    return;
  }

  switch (currentMode) {
    case MODE_MENU:
      selectMenuItem(menuIndex);
      break;

    case MODE_ALARM_ACTION: {
      // Edit alarm yang sudah ada: muat nilai lama lalu masuk layar edit
      int colon = alarmTimeStr.indexOf(':');
      if (colon != -1) {
        tempHour = alarmTimeStr.substring(0, colon).toInt();
        tempMinute = alarmTimeStr.substring(colon + 1).toInt();
      } else {
        tempHour = 0;
        tempMinute = 0;
      }
      alarmEditField = 0;
      currentMode = MODE_SET_ALARM;
      Serial.println("Edit alarm yang ada");
      break;
    }

    case MODE_SET_ALARM:
      if (alarmEditField == 0) {
        tempHour = (tempHour + 1) % 24;
      } else {
        tempMinute = (tempMinute + 1) % 60;
      }
      break;

    case MODE_POMODORO_SET:
      pomodoroMinutes += POMODORO_STEP_MINUTES;
      if (pomodoroMinutes > POMODORO_MAX_MINUTES) pomodoroMinutes = POMODORO_MIN_MINUTES;
      break;

    case MODE_SET_MQTT_HOST:
      mqttEditOctets[mqttOctetIndex] = (mqttEditOctets[mqttOctetIndex] + 1) % 256;
      break;

    case MODE_SET_MQTT_PORT:
      mqttUseTLS = !mqttUseTLS;
      break;

    case MODE_WIFI_RESET_CONFIRM:
      wifiResetRequested = true;
      break;

    default:
      break;
  }
}

void updateButtons() {
  unsigned long now = millis();

  int reading1 = digitalRead(PIN_BUTTON_1);
  if (reading1 != lastBtn1State && (now - lastBtn1Time) > BUTTON_DEBOUNCE_MS) {
    lastBtn1Time = now;
    lastBtn1State = reading1;
    if (reading1 == LOW) onButton1Press();
  }

  int reading2 = digitalRead(PIN_BUTTON_2);
  if (reading2 != lastBtn2State && (now - lastBtn2Time) > BUTTON_DEBOUNCE_MS) {
    lastBtn2Time = now;
    lastBtn2State = reading2;
    if (reading2 == LOW) onButton2Press();
  }

  int reading3 = digitalRead(PIN_BUTTON_3);
  if (reading3 != lastBtn3State && (now - lastBtn3Time) > BUTTON_DEBOUNCE_MS) {
    lastBtn3Time = now;
    lastBtn3State = reading3;
    if (reading3 == LOW) onButton3Press();
  }

  // Auto-repeat khusus PB3 saat mengedit oktet IP (range 0-255 lama kalau ditekan satu-satu).
  // Tahan PB3 > 500ms akan mulai increment otomatis tiap 80ms sampai dilepas.
  static unsigned long btn3HoldStart = 0;
  static unsigned long lastAutoRepeat = 0;
  bool btn3CurrentlyHeld = (digitalRead(PIN_BUTTON_3) == LOW);

  if (currentMode == MODE_SET_MQTT_HOST) {
    if (btn3CurrentlyHeld) {
      if (btn3HoldStart == 0) btn3HoldStart = now;
      if ((now - btn3HoldStart) > 500 && (now - lastAutoRepeat) > 80) {
        lastAutoRepeat = now;
        mqttEditOctets[mqttOctetIndex] = (mqttEditOctets[mqttOctetIndex] + 1) % 256;
      }
    } else {
      btn3HoldStart = 0;
    }
  } else {
    btn3HoldStart = 0;
  }
}

void updateBuzzerLogic(bool gasLeak) {
  if (gasLeak) {
    static unsigned long lastPulseTimeGas = 0;
    static bool pulseStateGas = false;
    if (millis() - lastPulseTimeGas > 150) {
      lastPulseTimeGas = millis();
      pulseStateGas = !pulseStateGas;
      setBuzzerState(pulseStateGas);
    }
  } else if (isAlarmActive) {
    static unsigned long lastPulseTimeAlarm = 0;
    static bool pulseStateAlarm = false;
    if (millis() - lastPulseTimeAlarm > 500) {
      lastPulseTimeAlarm = millis();
      pulseStateAlarm = !pulseStateAlarm;
      setBuzzerState(pulseStateAlarm);
    }
  } else if (isPomodoroRinging) {
    static unsigned long lastPulseTimePomo = 0;
    static bool pulseStatePomo = false;
    if (millis() - lastPulseTimePomo > 300) {
      lastPulseTimePomo = millis();
      pulseStatePomo = !pulseStatePomo;
      setBuzzerState(pulseStatePomo);
    }
  } else {
    setBuzzerState(false);
  }
}

// ==========================================
// 6. WIFI SETUP & CAPTIVE PORTAL MODULE
// ==========================================
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("Entered WiFi config mode");
  showSetupScreen(myWiFiManager->getConfigPortalSSID().c_str());
}

bool shouldSaveConfig = false;
void saveConfigCallback() {
  shouldSaveConfig = true;
}

void setupWiFi() {
  preferences.begin("mqtt-config", false);
  String load_host = preferences.getString("host", "");
  String load_port = preferences.getString("port", "8883");
  String load_user = preferences.getString("user", "");
  String load_pass = preferences.getString("pass", "");
  String load_devid = preferences.getString("devid", "esp32-clock-01");
  preferences.end();

  load_host.toCharArray(mqtt_host, 80);
  load_port.toCharArray(mqtt_port, 6);
  load_user.toCharArray(mqtt_user, 40);
  load_pass.toCharArray(mqtt_pass, 40);
  load_devid.toCharArray(device_id, 40);

  WiFiManager wifiManager;
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setConnectTimeout(10);

  WiFiManagerParameter custom_mqtt_host("server", "MQTT Host", mqtt_host, 80);
  WiFiManagerParameter custom_mqtt_port("port", "MQTT Port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("user", "MQTT User", mqtt_user, 40);
  WiFiManagerParameter custom_mqtt_pass("pass", "MQTT Password", mqtt_pass, 40);
  WiFiManagerParameter custom_device_id("device", "Device ID", device_id, 40);

  wifiManager.addParameter(&custom_mqtt_host);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_pass);
  wifiManager.addParameter(&custom_device_id);

  showStatusScreen("Koneksi WiFi", "Menghubungkan...");

  if (!wifiManager.autoConnect("SmartClock-Setup")) {
    Serial.println("WiFi auto-connect failed. Restarting...");
    showStatusScreen("Gagal WiFi", "Restart sistem...");
    delay(3000);
    ESP.restart();
    delay(5000);
  }

  Serial.println("Connected to WiFi!");
  showStatusScreen("WiFi OK", "Terhubung!");
  delay(800);

  if (shouldSaveConfig) {
    String cleanHost = String(custom_mqtt_host.getValue());
    cleanHost.trim();
    cleanHost.replace("https://", "");
    cleanHost.replace("http://", "");
    while (cleanHost.endsWith("/")) cleanHost.remove(cleanHost.length() - 1);
    cleanHost.toCharArray(mqtt_host, 80);

    strcpy(mqtt_port, custom_mqtt_port.getValue());

    String cleanUser = String(custom_mqtt_user.getValue());
    cleanUser.trim();
    cleanUser.toCharArray(mqtt_user, 40);

    String cleanPass = String(custom_mqtt_pass.getValue());
    cleanPass.trim();
    cleanPass.toCharArray(mqtt_pass, 40);

    strcpy(device_id, custom_device_id.getValue());

    preferences.begin("mqtt-config", false);
    preferences.putString("host", mqtt_host);
    preferences.putString("port", mqtt_port);
    preferences.putString("user", mqtt_user);
    preferences.putString("pass", mqtt_pass);
    preferences.putString("devid", device_id);
    preferences.end();
    Serial.println("WiFi settings saved successfully.");
  }
}

void resetWiFiSettings() {
  WiFiManager wifiManager;
  wifiManager.resetSettings();

  preferences.begin("mqtt-config", false);
  preferences.clear();
  preferences.end();

  showStatusScreen("Reset WiFi", "Restart sistem...");
  delay(2000);
  ESP.restart();
}

bool isWiFiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

// ==========================================
// 7. MQTT CLIENT MODULE
// ==========================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Received MQTT command: ");
  Serial.println(message);

  int alarmTimeKeyIndex = message.indexOf("set_alarm_time");
  if (alarmTimeKeyIndex != -1) {
    int firstQuote = message.indexOf('"', alarmTimeKeyIndex + 14);
    if (firstQuote != -1) {
      int secondQuote = message.indexOf('"', firstQuote + 1);
      if (secondQuote != -1) {
        String alarmTimeVal = message.substring(firstQuote + 1, secondQuote);
        setAlarmTime(alarmTimeVal);
        Serial.print("MQTT set alarm: ");
        Serial.println(alarmTimeVal);
      }
    }
  }

  int clearAlarmKeyIndex = message.indexOf("clear_alarm");
  if (clearAlarmKeyIndex != -1) {
    clearAlarm();
    Serial.println("MQTT alarm cleared");
  }

  int triggerBuzzerKeyIndex = message.indexOf("trigger_buzzer");
  if (triggerBuzzerKeyIndex != -1) {
    int valueIndex = message.indexOf(':', triggerBuzzerKeyIndex);
    if (valueIndex != -1) {
      String subStr = message.substring(valueIndex + 1);
      subStr.trim();
      if (subStr.startsWith("true")) {
        isAlarmActive = true;
        manualAlarmSilence = false;
        Serial.println("MQTT alarm triggered");
      } else if (subStr.startsWith("false")) {
        isAlarmActive = false;
        manualAlarmSilence = true;
        setBuzzerState(false);
        Serial.println("MQTT alarm silenced");
      }
    }
  }

  int pomodoroKeyIndex = message.indexOf("start_pomodoro");
  if (pomodoroKeyIndex != -1) {
    int valueIndex = message.indexOf(':', pomodoroKeyIndex);
    if (valueIndex != -1) {
      String subStr = message.substring(valueIndex + 1);
      int minutes = subStr.toInt();
      if (minutes > 0) {
        pomodoroMinutes = minutes;
        startPomodoro();
        Serial.print("MQTT pomodoro started: ");
        Serial.println(minutes);
      }
    }
  }

  int stopPomodoroKeyIndex = message.indexOf("stop_pomodoro");
  if (stopPomodoroKeyIndex != -1) {
    stopPomodoro();
    currentMode = MODE_CLOCK;
    Serial.println("MQTT pomodoro stopped");
  }
}

// Helper: bersihkan mqtt_host dari prefix protokol (case-insensitive) dan trailing slash.
// Menggunakan buffer static sehingga pointer yang dikembalikan selalu valid.
const char* getCleanMqttHost() {
  static char cleaned[80];
  const char* src = mqtt_host;

  if (strncasecmp(src, "https://", 8) == 0) src += 8;
  else if (strncasecmp(src, "http://", 7) == 0) src += 7;

  strncpy(cleaned, src, sizeof(cleaned) - 1);
  cleaned[sizeof(cleaned) - 1] = '\0';

  // Buang trailing slash (mis. "xxx.hivemq.cloud/" → "xxx.hivemq.cloud")
  size_t len = strlen(cleaned);
  while (len > 0 && cleaned[len - 1] == '/') {
    cleaned[len - 1] = '\0';
    len--;
  }

  return cleaned;
}

void initMQTT() {
  int port = atoi(mqtt_port);

  if (port == 8883) {
    secureClient.setInsecure(); // TLS tanpa verifikasi sertifikat (cukup untuk project personal)
    secureClient.setTimeout(30); // Timeout 30 detik untuk TLS handshake di ESP32
    mqttClient = new PubSubClient(secureClient);
  } else {
    mqttClient = new PubSubClient(normalClient);
  }
  mqttClient->setServer(getCleanMqttHost(), port);
  mqttClient->setCallback(mqttCallback);
  mqttClient->setBufferSize(512); // Buffer tambahan untuk MQTT TLS di ESP32
}

bool connectMQTT() {
  if (mqtt_host[0] == '\0') return false;

  Serial.print("Connecting to MQTT: ");
  Serial.print(getCleanMqttHost());
  Serial.print(":");
  Serial.println(mqtt_port);

  // Diagnostic tambahan sebelum connect
  Serial.print("WiFi status: ");
  Serial.println(isWiFiConnected() ? "Connected" : "NOT connected");
  Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());
  Serial.print("MQTT User: [");
  Serial.print(mqtt_user);
  Serial.println("]");

  String statusTopic = "smartclock/" + String(device_id) + "/status";
  String lwtPayload = "{\"device_id\":\"" + String(device_id) + "\",\"online\":false}";

  boolean connected = false;
  if (mqtt_user[0] != '\0') {
    connected = mqttClient->connect(device_id, mqtt_user, mqtt_pass, statusTopic.c_str(), 1, true, lwtPayload.c_str());
  } else {
    connected = mqttClient->connect(device_id, statusTopic.c_str(), 1, true, lwtPayload.c_str());
  }

  if (connected) {
    Serial.println("MQTT Connected.");
    String commandTopic = "smartclock/" + String(device_id) + "/command";
    mqttClient->subscribe(commandTopic.c_str(), 1);
    publishStatus(0, isAlarmActive);
  } else {
    // Cetak kode error spesifik dari PubSubClient
    Serial.print("MQTT connect FAILED, state code: ");
    Serial.println(mqttClient->state());
  }
  return connected;
}

void updateMQTT() {
  if (!mqttClient) return;

  if (!mqttClient->connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      if (connectMQTT()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    mqttClient->loop();
  }
}

void publishStatus(int gasLevel, bool alarmActive) {
  if (!mqttClient || !mqttClient->connected()) return;

  String statusTopic = "smartclock/" + String(device_id) + "/status";
  time_t now;
  time(&now);

  String payload = "{";
  payload += "\"device_id\":\"" + String(device_id) + "\",";
  payload += "\"gas_level\":" + String(gasLevel) + ",";
  payload += "\"alarm_active\":" + String(alarmActive ? "true" : "false") + ",";
  payload += "\"alarm_time\":\"" + alarmTimeStr + "\",";
  payload += "\"pomodoro_running\":" + String(currentMode == MODE_POMODORO_RUNNING ? "true" : "false") + ",";
  payload += "\"online\":true,";
  payload += "\"timestamp\":" + String((unsigned long)now);
  payload += "}";

  mqttClient->publish(statusTopic.c_str(), payload.c_str(), true);
  Serial.print("MQTT published: ");
  Serial.println(payload);
}

bool isMQTTConnected() {
  return mqttClient && mqttClient->connected();
}

void timeSyncCallback(struct timeval *tv) {
  Serial.println("Time synchronized via NTP successfully!");
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    Serial.print("Synchronized Time: ");
    Serial.println(asctime(&timeinfo));
  }
}

// ==========================================
// 8. ARDUINO MAIN SETUP & LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
  Serial.println("Smart Clock initializing...");

  initDisplay();
  initAlarmAndButtons();
  initGasSensor();

  setupWiFi();

  sntp_set_time_sync_notification_cb(timeSyncCallback);
  configTime(7 * 3600, 0, "id.pool.ntp.org", "time.google.com", "pool.ntp.org");
  Serial.println("NTP sync started (id.pool.ntp.org).");

  initMQTT();
}

void loop() {
  if (wifiResetRequested) {
    wifiResetRequested = false;
    resetWiFiSettings();
  }

  updateMQTT();
  updateButtons();

  int gasLevel = readGasLevel();
  bool gasLeak = isGasLeaking();
  updateBuzzerLogic(gasLeak);

  char timeStr[6];
  char dateStr[20];
  struct tm timeinfo;
  bool timeSyncSuccess = getLocalTime(&timeinfo);

  if (timeSyncSuccess) {
    strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
    strftime(dateStr, sizeof(dateStr), "%d/%m", &timeinfo);
    checkAlarmTrigger(timeinfo.tm_hour, timeinfo.tm_min);
  } else {
    strcpy(timeStr, "--:--");
    strcpy(dateStr, "-");
  }

  if (currentMode == MODE_POMODORO_RUNNING && millis() >= pomodoroEndMillis) {
    currentMode = MODE_POMODORO_DONE;
    isPomodoroRinging = true;
    Serial.println("Pomodoro selesai!");
  }

  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 150) {
    lastDisplayUpdate = millis();

    if (gasLeak) {
      showGasAlertScreen(gasLevel);
    } else if (isAlarmActive && currentMode == MODE_CLOCK) {
      showAlarmRingingScreen(timeStr);
    } else {
      switch (currentMode) {
        case MODE_CLOCK:
          showClockScreen(timeStr, dateStr, gasLevel, alarmTimeStr != "", alarmTimeStr.c_str(), isWiFiConnected(), isMQTTConnected());
          break;

        case MODE_MENU:
          showMenuScreen(menuIndex);
          break;

        case MODE_ALARM_ACTION:
          showAlarmActionScreen(alarmTimeStr.c_str());
          break;

        case MODE_SET_ALARM:
          showSetAlarmScreen(tempHour, tempMinute, alarmEditField);
          break;

        case MODE_POMODORO_SET:
          showPomodoroSetScreen(pomodoroMinutes);
          break;

        case MODE_POMODORO_RUNNING: {
          long remainingMs = (long)(pomodoroEndMillis - millis());
          if (remainingMs < 0) remainingMs = 0;
          showPomodoroRunningScreen(remainingMs / 1000);
          break;
        }

        case MODE_POMODORO_DONE:
          showPomodoroDoneScreen();
          break;

        case MODE_SET_MQTT_HOST:
          showSetMqttHostScreen(mqttEditOctets, mqttOctetIndex);
          break;

        case MODE_SET_MQTT_PORT:
          showSetMqttPortScreen(mqttUseTLS);
          break;

        case MODE_WIFI_RESET_CONFIRM:
          showWifiResetConfirmScreen();
          break;
      }
    }
  }

  unsigned long now = millis();
  bool gasLeakTransition = (gasLeak != lastGasLeakState);
  bool alarmStateTransition = (isAlarmActive != lastAlarmActiveState);

  if ((now - lastHeartbeat > HEARTBEAT_INTERVAL_MS) || gasLeakTransition || alarmStateTransition) {
    lastHeartbeat = now;
    lastGasLeakState = gasLeak;
    lastAlarmActiveState = isAlarmActive;
    publishStatus(gasLevel, isAlarmActive);
  }

  delay(10);
}