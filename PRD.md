# Product Requirement Document (PRD)
## Proyek UAS: Smart Digital Clock & Alarm IoT (WhatsApp Integrated)

> **Catatan untuk AI Code Assistant (Claude Code / Cursor / Codex):**
> Dokumen ini adalah single source of truth untuk dua codebase yang saling terhubung:
> 1. **Firmware ESP32** (C++, Arduino Framework / PlatformIO)
> 2. **WhatsApp Gateway Server** (Node.js, `wwebjs.dev` + MQTT client)
>
> Kedua sisi WAJIB mengikuti kontrak topic & payload MQTT di Bagian 6 agar tidak terjadi mismatch. Jika ada ambiguitas, prioritaskan konsistensi terhadap tabel pin di Bagian 3 (hardware, TIDAK berubah) dan kontrak MQTT di Bagian 6 — jangan berasumsi nama topic/payload baru tanpa konfirmasi.
>
> **Perubahan arsitektur:** Komunikasi ESP32 ↔ Gateway dipindah dari REST polling ke **MQTT (publish/subscribe)**. Ini murni perubahan software (firmware + gateway) — tidak ada perubahan wiring, pin, atau komponen hardware dari versi sebelumnya.

---

## 1. Ringkasan Proyek

Sistem ini merupakan jam digital berbasis IoT yang dilengkapi dengan fitur alarm, sensor kebocoran gas/asap, layar informasi OLED, serta interaksi dua arah menggunakan WhatsApp Bot melalui Cloudflare Tunnel. Sirkuit diimplementasikan menggunakan PCB Polos hasil cetak mandiri (CNC Router) dengan tambahan 8 buah LED dekorasi yang dikendalikan secara efisien oleh IC Shift Register 74LS164.

ESP32 dilengkapi **WiFiManager** (captive portal) sehingga kredensial WiFi tidak di-hardcode di source code — memudahkan pergantian jaringan WiFi di kemudian hari tanpa perlu re-flash firmware.

Komunikasi antara ESP32 dan WhatsApp Gateway menggunakan protokol **MQTT** (publish/subscribe) melalui broker MQTT cloud pihak ketiga, sehingga ESP32 tetap **outbound-only** (aman di belakang NAT rumah tanpa perlu Cloudflare Tunnel tambahan) dan mendukung komunikasi near-real-time dua arah.

---

## 2. Kebutuhan Perangkat Keras & Komponen

* **Mikrokontroler:** ESP32 WROOM (30 Pin/38 Pin NodeMCU layout)
* **Logika Perluasan Output:** IC SN74LS164N (14-Pin DIP Package)
* **Display:** Layar OLED 0.96 inch I2C (SSD1306)
* **Sensor:** Sensor Gas/Asap MQ Series (Analog Output)
* **Indikator Suara:** Active Buzzer 5V
* **Input Fisik:** 3x Push Button (menggunakan internal `INPUT_PULLUP`)
* **Dekorasi:** 8x LED Standard + 1x Resistor Metal Film 1k Ohm (Common Cathode Ground)
* **Reset WiFi Manual (baru):** Kombinasi tombol existing (long-press Push Button 3 di GPIO 14 selama ≥3 detik) digunakan untuk trigger `wifiManager.resetSettings()` — tidak perlu tombol fisik tambahan.

---

## 3. Peta Wiring & Koneksi Pin Definitif (Anti-Salah)

### A. Tabel Sisi Kiri ESP32

| Pin ESP32 | Jenis Pin | Terhubung ke Komponen | Keterangan Fungsi |
| :--- | :--- | :--- | :--- |
| **3V3** | Power Output | VCC Layar OLED | Suplai daya logika OLED |
| **GND** | Ground Bus | GND Bersama (OLED, Tombol, Sensor, Buzzer, IC) | Jalur balik daya utama |
| **GPIO 34 (VP)** | Analog In | Pin AOUT Sensor MQ | Membaca kadar gas/asap |
| **GPIO 25** | Digital Out | **Pin 1 & Pin 2 IC 74LS164** | Jalur Serial DATA (A & B digabung) |
| **GPIO 26** | Digital In | Push Button 1 | Tombol manual Matikan Alarm |
| **GPIO 27** | Digital Out | Positif (+) Active Buzzer | Pemicu suara alarm |
| **GPIO 14** | Digital In | Push Button 3 | Tombol menu fungsional + long-press = reset WiFi |
| **GPIO 12** | Digital In | Push Button 2 | Tombol menu fungsional |
| **5V / VIN** | Power Input | VCC Sensor MQ, Pin 14 (VCC) IC, Pin 9 (CLR) IC | Suplai utama 5V dari USB |

### B. Tabel Sisi Kanan ESP32

| Pin ESP32 | Jenis Pin | Terhubung ke Komponen | Keterangan Fungsi |
| :--- | :--- | :--- | :--- |
| **GPIO 22** | I2C SCL | Pin SCL Layar OLED | Jalur Clock Display |
| **GPIO 21** | I2C SDA | Pin SDA Layar OLED | Jalur Data Display |
| **GPIO 18** | Digital Out | **Pin 8 (CLK) IC 74LS164** | Jalur pergeseran sinyal Clock IC |

### C. Pemetaan Kaki IC SN74LS164N (14 Pin) ke 8 LED Dekorasi

| Nomor Pin IC | Nama Pin | Hubungkan ke Mana? | Target Output Fisik |
| :---: | :--- | :--- | :--- |
| **1** | A (Data Input 1) | Jumper langsung ke Pin 2 IC | Input Serial |
| **2** | B (Data Input 2) | Jalur menuju **GPIO 25** ESP32 | Input Serial |
| **3** | Q0 (Output) | Kaki Anoda (+) LED 1 | LED Dekorasi 1 |
| **4** | Q1 (Output) | Kaki Anoda (+) LED 2 | LED Dekorasi 2 |
| **5** | Q2 (Output) | Kaki Anoda (+) LED 3 | LED Dekorasi 3 |
| **6** | Q3 (Output) | Kaki Anoda (+) LED 4 | LED Dekorasi 4 |
| **7** | **GND** | Jalur utama GND ESP32 | Ground IC |
| **8** | CLK (Clock) | Jalur menuju **GPIO 18** ESP32 | Sinyal Detak |
| **9** | CLR (Clear) | Jalur menuju **5V / VIN** ESP32 | Mencegah auto-reset (Active Low) |
| **10** | Q4 (Output) | Kaki Anoda (+) LED 5 | LED Dekorasi 5 |
| **11** | Q5 (Output) | Kaki Anoda (+) LED 6 | LED Dekorasi 6 |
| **12** | Q6 (Output) | Kaki Anoda (+) LED 7 | LED Dekorasi 7 |
| **13** | Q7 (Output) | Kaki Anoda (+) LED 8 | LED Dekorasi 8 |
| **14** | **VCC** | Jalur menuju **5V / VIN** ESP32 | Jalur daya utama IC (5V) |

> ⚠️ **PANDUAN JALUR KATODA LED (SOLUSI 1 RESISTOR):**
> Seluruh kaki Katoda (- / kaki pendek) dari LED 1 sampai LED 8 wajib disatukan menjadi satu jalur tembaga interkoneksi di PCB, lalu hubungkan ke salah satu kaki Resistor 1k Ohm. Kaki resistor yang satunya lagi ditarik langsung ke jalur GND ESP32.

---

## 4. Arsitektur Sistem (High-Level)

```
┌─────────────┐   publish/subscribe    ┌──────────────────────┐       publish/subscribe   ┌──────────────────────┐      WhatsApp Web       ┌──────────┐
│   ESP32     │ ──────────────────────▶│   MQTT Broker Cloud  │◀──────────────────────────│  Node.js Gateway     │ ───────────────────────▶│  User    │
│  Firmware   │◀────────────────────── │ (HiveMQ Cloud/EMQX)  │───────────────────────────▶│  (wwebjs.dev + PM2)  │◀─────────────────────── │  Phone   │
└─────────────┘   MQTT over TLS (8883) └──────────────────────┘                            └──────────────────────┘
      │
      │ WiFiManager captive portal
      ▼
  Setup WiFi via HP saat pertama
  kali nyala / tombol reset
```

* **ESP32** bertindak sebagai MQTT client: **subscribe** ke topic command (menerima perintah dari WhatsApp secara near-instant), dan **publish** ke topic status (melaporkan gas level, alarm state) secara periodik/event-based.
* **MQTT Broker** (cloud, contoh: HiveMQ Cloud atau EMQX Cloud, tier gratis cukup untuk 1 device) bertindak sebagai perantara pesan. Baik ESP32 maupun Node.js Gateway sama-sama **outbound-only** ke broker ini — **tidak perlu Cloudflare Tunnel untuk ESP32**, karena ESP32 tidak menerima koneksi masuk dari mana pun.
* **Node.js Gateway** bertindak sebagai jembatan antara WhatsApp dan MQTT: menerima pesan WhatsApp → publish command ke topic MQTT; subscribe topic status dari ESP32 → format balasan ke WhatsApp saat user `/status`.
* **Cloudflare Tunnel** yang sudah ada sebelumnya (`alarm.fachrigaffar.web.id`) sekarang **opsional** — hanya diperlukan jika gateway Node.js butuh diakses dari luar untuk keperluan lain (misal dashboard web monitoring), bukan untuk komunikasi ESP32 lagi.

---

## 5. Kebutuhan Perangkat Lunak & Jaringan

* **Firmware:** Arduino Framework (C++), disarankan pakai PlatformIO untuk manajemen dependency yang konsisten.
* **Library WiFi Manager:** `tzapu/WiFiManager` (via captive portal AP mode). Menggantikan hardcoded `ssid`/`password` di kode lama.
* **Library MQTT Client (BARU, menggantikan HTTP client):** `knolleary/PubSubClient` (ringan, umum dipakai) atau `AsyncMqttClient` (kalau butuh non-blocking penuh). Rekomendasi untuk skripsi: **PubSubClient**, karena lebih sederhana dijelaskan dan didukung banyak tutorial.
* **Koneksi MQTT:** Menggunakan `WiFiClientSecure` sebagai underlying transport untuk PubSubClient agar koneksi ke broker terenkripsi TLS di port `8883` (bukan port `1883` yang plain text).
* **MQTT Broker (cloud, pilih salah satu):**
  - **HiveMQ Cloud** (free tier: 100 koneksi, cukup untuk 1 device + 1 gateway)
  - **EMQX Cloud** (free tier serupa)
  - Kedua broker mendukung TLS dan autentikasi username/password per client.
* **WhatsApp Gateway:** Framework `wwebjs.dev` (Node.js) + library `mqtt` (npm package `mqtt`) sebagai MQTT client, dijalankan di bawah `PM2` agar tetap hidup saat terminal ditutup.
* **Domain Endpoint (opsional, untuk dashboard/monitoring):** `https://alarm.fachrigaffar.web.id` via Cloudflare Tunnel — tidak lagi dipakai untuk komunikasi ESP32.

### 5.1 Spesifikasi WiFiManager (Detail Implementasi)

* **Library:** `WiFiManager` by tzapu (install via Library Manager / PlatformIO `lib_deps`).
* **Mode Trigger Setup:**
  1. **Otomatis saat boot** jika tidak ada kredensial WiFi tersimpan atau gagal connect dalam `connectTimeout` (misal 10 detik) → device masuk **AP Mode** dengan SSID `SmartClock-Setup` (tanpa password, atau password default `12345678`).
  2. **Manual reset** via long-press Push Button 3 (GPIO 14) ≥3 detik → panggil `wifiManager.resetSettings()` lalu restart ESP32.
* **Portal Konfigurasi:** User connect ke AP `SmartClock-Setup` dari HP, browser otomatis diarahkan ke captive portal untuk memilih SSID rumah + masukkan password.
* **Custom Parameter Tambahan (opsional, direkomendasikan):** Tambahkan field custom di WiFiManager untuk `device_id` atau `gateway_api_key` sehingga tidak perlu hardcode di firmware, dan bisa diganti bersamaan saat setup WiFi.
* **Indikator OLED saat AP Mode:** Tampilkan teks di layar OLED seperti `"Setup WiFi: connect to SmartClock-Setup"` agar user tahu device sedang menunggu konfigurasi (bukan hang/rusak).
* **Non-blocking requirement:** Selama AP mode aktif, fungsi alarm/buzzer/LED tetap boleh idle, tapi TIDAK boleh crash — pastikan `wifiManager.autoConnect()` dipanggil di `setup()` sebelum inisialisasi loop utama.

---

## 6. Kontrak MQTT (WAJIB Konsisten Antara Firmware & Gateway)

> Bagian ini penting agar AI assistant yang mengerjakan sisi C++ dan sisi Node.js menghasilkan topic & payload yang saling cocok. Sesuaikan/lengkapi field sesuai kebutuhan asli sebelum mulai coding, lalu JANGAN diubah sepihak di salah satu sisi tanpa update dokumen ini.

### 6.1 Konvensi Topic

Gunakan prefix per-device supaya scalable kalau nanti ada lebih dari 1 alat:

| Topic | Arah | Publisher | Subscriber |
| :--- | :--- | :--- | :--- |
| `smartclock/{device_id}/status` | ESP32 → Gateway | ESP32 | Node.js Gateway |
| `smartclock/{device_id}/command` | Gateway → ESP32 | Node.js Gateway | ESP32 |

Contoh `device_id`: `esp32-clock-01` (disimpan sebagai custom parameter WiFiManager, lihat 5.1, sehingga tidak hardcode).

### 6.2 ESP32 → Gateway: Publish Status

**Topic:** `smartclock/esp32-clock-01/status`
**QoS:** 1 (at-least-once, supaya tidak ada status yang hilang)
**Retain:** `true` (agar gateway bisa langsung ambil status terakhir saat baru subscribe/reconnect)

```json
{
  "device_id": "esp32-clock-01",
  "gas_level": 812,
  "alarm_active": false,
  "timestamp": 1720000000
}
```

**Trigger publish:** setiap perubahan state penting (alarm nyala/mati, gas level naik signifikan) **dan** heartbeat berkala (misal tiap 30 detik) agar gateway tahu device masih online.

### 6.3 Gateway → ESP32: Publish Command

**Topic:** `smartclock/esp32-clock-01/command`
**QoS:** 1
**Retain:** `false` (command sekali eksekusi, bukan state permanen)

```json
{
  "set_alarm_time": "06:30",
  "trigger_buzzer": false,
  "led_pattern": "idle"
}
```

ESP32 **subscribe** ke topic ini di `setup()` (setelah WiFi & MQTT connect), lalu proses payload di MQTT callback function.

### 6.4 Last Will and Testament (LWT) — Deteksi Device Offline

Saat ESP32 connect ke broker, set LWT agar broker otomatis publish pesan berikut jika ESP32 terputus tiba-tiba (misal listrik mati/WiFi putus):

**Topic:** `smartclock/esp32-clock-01/status`
**Payload LWT:** `{"device_id": "esp32-clock-01", "online": false}`
**Retain:** `true`

Gateway subscribe topic ini juga untuk kasih tahu user via WhatsApp kalau device offline saat `/status` diminta.

### 6.5 WhatsApp User → Gateway → Publish ke Topic Command

Contoh perintah WhatsApp yang perlu di-parsing oleh gateway (`wwebjs.dev` message handler), lalu di-translate jadi publish MQTT ke topic 6.3:
* `/alarm 06:30` → publish `{"set_alarm_time": "06:30"}`
* `/status` → gateway baca retained message dari topic 6.2 (tidak perlu tanya ESP32 langsung), balas ringkasan ke WhatsApp
* `/matikan` → publish `{"trigger_buzzer": false}`

### 6.6 Autentikasi & Keamanan

* Setiap client (ESP32 & Gateway) connect ke broker pakai **username/password terpisah** yang disediakan broker cloud (HiveMQ/EMQX), bukan API key custom buatan sendiri.
* Koneksi wajib via **TLS port 8883**, bukan `1883` plain text.
* Broker cloud modern (HiveMQ Cloud/EMQX Cloud) juga mendukung **ACL per client** — batasi ESP32 hanya boleh publish ke topic `status` dan subscribe ke topic `command` miliknya sendiri, tidak bisa akses topic device lain.

> **Catatan:** Kalau kamu ganti provider broker atau ubah struktur topic, edit Bagian 6 ini dulu sebelum minta AI generate kode — supaya firmware dan gateway tetap sinkron.

---

## 7. Instruksi Khusus untuk Pembuatan Jalur PCB (CNC Router)

1. **Ketebalan Jalur (Trace Width):** Gunakan ketebalan minimal `0.6 mm` hingga `1.0 mm` untuk jalur daya (VCC 5V dan GND) karena memikul beban sensor gas MQ, buzzer, dan 8 LED. Jalur data logika bisa menggunakan ukuran standar `0.4 mm`.
2. **Trace Clearance:** Berikan jarak aman (clearance) antar jalur tembaga minimal `0.5 mm` untuk mencegah short circuit akibat sisa serpihan tembaga saat proses *milling* oleh mata bor CNC Router.
3. **Jumper Tembaga Terdekat:** Gabungkan Pad Pin 1 dan Pin 2 IC 74LS164 di layer bawah menggunakan trace terpendek sebelum ditarik keluar menuju GPIO 25. Lakukan hal sama untuk Pin 14 dan Pin 9 menuju suplai 5V.

---

## 8. Struktur Kode yang Disarankan (untuk AI Code Assistant)

### 8.1 Firmware ESP32 (PlatformIO project layout)

```
/src
  main.cpp              # setup(), loop(), orchestrator
  wifi_setup.cpp/.h      # wrapper WiFiManager (autoConnect, resetSettings, custom params: device_id, mqtt user/pass)
  mqtt_client.cpp/.h     # koneksi PubSubClient + WiFiClientSecure (TLS 8883), subscribe command, publish status, LWT
  display.cpp/.h         # OLED SSD1306 rendering (termasuk teks saat AP mode WiFiManager)
  shift_register.cpp/.h  # kontrol 74LS164 (shiftOut ke GPIO 25 & 18)
  alarm.cpp/.h           # logic buzzer + tombol
  gas_sensor.cpp/.h      # baca GPIO 34, kalkulasi threshold
platformio.ini            # tambahkan lib_deps: WiFiManager, PubSubClient
```

### 8.2 Gateway Node.js (layout)

```
/src
  index.js              # entrypoint, PM2 ready
  whatsapp/client.js     # inisialisasi wwebjs.dev client
  whatsapp/commands.js   # parsing /alarm, /status, /matikan → publish ke MQTT
  mqtt/client.js         # koneksi npm `mqtt` ke broker (TLS 8883), subscribe status, publish command
  store/deviceState.js   # cache retained status terakhir per device_id (in-memory)
.env                     # MQTT_HOST, MQTT_PORT, MQTT_USER, MQTT_PASS, dll
```

---

## 9. Prioritas Pengerjaan (Checklist untuk Skripsi/UAS)

- [ ] Setup akun broker MQTT cloud (HiveMQ Cloud/EMQX Cloud) + buat 2 client credential (ESP32 & Gateway)
- [ ] Firmware: integrasi WiFiManager menggantikan hardcoded credentials
- [ ] Firmware: integrasi PubSubClient (TLS 8883) — connect, subscribe topic command, publish topic status, setup LWT
- [ ] Firmware: shift register 74LS164 untuk 8 LED
- [ ] Firmware: baca sensor gas + trigger buzzer lokal
- [ ] Gateway: setup wwebjs.dev + PM2
- [ ] Gateway: integrasi npm `mqtt` — subscribe topic status, publish topic command sesuai kontrak Bagian 6
- [ ] Gateway: command parser WhatsApp (/alarm, /status, /matikan)
- [ ] Integrasi end-to-end test: WhatsApp → Gateway → MQTT Broker → ESP32 → Buzzer/LED
- [ ] Test skenario offline: cabut power ESP32, pastikan LWT ter-trigger dan gateway lapor "device offline" saat `/status`
- [ ] Dokumentasi PCB layout final untuk lampiran laporan