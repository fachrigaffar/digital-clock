# PRD: Kontrol Smart Clock via WhatsApp (WWebJS Bridge)

**Dokumen:** Product Requirements Document
**Produk:** Smart Clock (ESP32 Gas Alarm + Alarm Jam + Pomodoro)
**Fitur baru:** Kontrol & notifikasi via WhatsApp
**Status:** Draft
**Versi:** 0.1

---

## 1. Latar Belakang

Smart Clock saat ini adalah perangkat ESP32 dengan:
- Sensor gas (MQ series) dengan alarm buzzer otomatis saat level gas melewati ambang batas
- Alarm jam manual (set/edit/hapus via OLED + 3 push button)
- Timer Pomodoro
- OLED 128x32 sebagai antarmuka utama
- Koneksi WiFi (via WiFiManager) dan MQTT (TLS, port 8883) untuk publish status & menerima command

Saat ini kontrol jarak jauh hanya bisa lewat MQTT client/dashboard (mis. MQTT Explorer, Node-RED, dsb), yang kurang praktis untuk pemakaian sehari-hari. Tujuan fitur baru ini adalah memungkinkan user **mengontrol dan menerima notifikasi Smart Clock langsung dari WhatsApp**, tanpa perlu membuka aplikasi/dashboard terpisah.

## 2. Tujuan (Goals)

1. User bisa mengatur alarm, memulai/menghentikan Pomodoro, dan mengecek status perangkat (WiFi, gas, alarm) lewat chat WhatsApp.
2. User menerima **notifikasi otomatis** ke WhatsApp saat kejadian penting: gas bocor terdeteksi, alarm jam berbunyi, Pomodoro selesai.
3. Fase awal berjalan sepenuhnya lokal (laptop + broker MQTT lokal), tanpa dependensi cloud, agar mudah dikembangkan & diuji.
4. Arsitektur dirancang agar broker MQTT lokal bisa diganti ke MQTT cloud/online di kemudian hari **tanpa mengubah kode firmware ESP32 maupun logika command WhatsApp** — hanya konfigurasi host/port/kredensial yang berubah.

## 3. Non-Goals (Di Luar Cakupan Fase Ini)

- Tidak membangun UI web/dashboard terpisah (WhatsApp adalah satu-satunya antarmuka jarak jauh untuk fase ini).
- Tidak mendukung multi-user/multi-device dari awal — asumsikan satu nomor WhatsApp admin per perangkat di fase 1.
- Tidak menangani autentikasi WhatsApp Business API resmi (Meta Cloud API) — fase ini pakai `whatsapp-web.js` (wwebjs) yang meniru sesi WhatsApp Web biasa.
- Tidak menjamin uptime 24/7 di fase lokal, karena bridge berjalan di laptop (mati kalau laptop mati/tidur).

## 4. Pengguna & Use Case

**Persona:** Pemilik perangkat (single user), mengontrol Smart Clock dari HP-nya sendiri via WhatsApp, baik saat di rumah maupun (nanti, di fase cloud) saat di luar.

**Use case utama:**
1. "Set alarm jam 6 pagi" → alarm tersimpan di perangkat, konfirmasi dibalas ke WhatsApp.
2. "Mulai pomodoro 25 menit" → timer dimulai, notifikasi dikirim saat selesai.
3. "Cek status" → dibalas ringkasan: level gas, status WiFi/MQTT, alarm aktif, sisa waktu pomodoro.
4. Gas bocor terdeteksi → tanpa diminta, sistem langsung push notifikasi WhatsApp "⚠️ Gas bocor terdeteksi! Level: 512".
5. "Matikan alarm" / "Hapus alarm" / "Batal pomodoro" → aksi langsung dieksekusi di perangkat.

## 5. Arsitektur Sistem

```
[ESP32 Smart Clock] <--MQTT(TLS)--> [Broker MQTT Lokal] <--MQTT--> [Bridge Node.js: wwebjs + MQTT client] <--WhatsApp Web protocol--> [WhatsApp User]
```

### Komponen

| Komponen | Fase 1 (Lokal) | Fase 2 (Cloud, nanti) |
|---|---|---|
| Broker MQTT | Mosquitto lokal di laptop (`localhost:8883` atau `1883` non-TLS untuk simplifikasi awal) | Broker cloud (HiveMQ Cloud, EMQX Cloud, atau self-hosted VPS) |
| Bridge WhatsApp | Node.js + `whatsapp-web.js` + `mqtt` npm package, berjalan di laptop yang sama | Bisa tetap di laptop, atau dipindah ke VPS/server kecil (Raspberry Pi, VPS murah) |
| Firmware ESP32 | Tidak berubah — cukup ganti `mqtt_host`/`mqtt_port` di WiFiManager config portal | Sama, tinggal ganti host lewat portal setup (tombol PB3 lama-tekan sudah tidak ada; gunakan menu Reset WiFi untuk masuk ulang ke portal) |

Poin kunci desain: **firmware ESP32 tidak perlu tahu soal WhatsApp sama sekali.** Ia hanya bicara MQTT seperti sekarang. Semua logika WhatsApp ada di bridge Node.js, sehingga:
- Mengganti broker lokal → cloud hanya soal konfigurasi ulang host MQTT di 2 tempat: ESP32 (via WiFiManager portal) dan bridge Node.js (`.env`).
- Bridge bisa di-restart/di-deploy ulang tanpa mengganggu firmware.

## 6. Topik & Payload MQTT (Kontrak Existing — Referensi)

Firmware saat ini sudah mendukung topik berikut (device_id default: `esp32-clock-01`):

**Publish oleh device** → `smartclock/{device_id}/status` (retained):
```json
{
  "device_id": "esp32-clock-01",
  "gas_level": 123,
  "alarm_active": false,
  "alarm_time": "06:00",
  "pomodoro_running": false,
  "online": true,
  "timestamp": 1234567890
}
```
Dipublish tiap 30 detik (heartbeat) atau saat ada perubahan state (gas leak transition, alarm state transition).

**Subscribe oleh device** ← `smartclock/{device_id}/command`, payload JSON manual-parsed, mendukung key berikut:
- `set_alarm_time`: `{"set_alarm_time":"06:30"}`
- `clear_alarm`: `{"clear_alarm":true}`
- `trigger_buzzer`: `{"trigger_buzzer":true}` / `false`
- `start_pomodoro`: `{"start_pomodoro":25}` (angka = menit)

**LWT (Last Will):** `{"device_id":"...","online":false}` di topic status saat device disconnect tak terduga.

> Catatan: command MQTT saat ini di-parse manual (string search), bukan JSON library. Untuk fitur WhatsApp, bridge cukup mengirim payload JSON sesuai format di atas — tidak perlu mengubah firmware.

## 7. Requirement Fungsional — Bridge WhatsApp

### 7.1 Koneksi & Autentikasi
- FR-1: Bridge menggunakan `whatsapp-web.js`, autentikasi via scan QR code sekali (disimpan session lokal via `LocalAuth` strategy agar tidak perlu scan ulang tiap restart).
- FR-2: Bridge hanya merespons pesan dari nomor WhatsApp yang terdaftar di whitelist (`ALLOWED_NUMBERS` di config), untuk mencegah orang lain mengontrol perangkat.

### 7.2 Command Parsing
- FR-3: Bridge mendukung command berbasis teks natural sederhana (bukan harus command persis), contoh pola yang harus dikenali:
  - `set alarm 06:00` / `set alarm jam 6 pagi` → publish `set_alarm_time`
  - `hapus alarm` / `batal alarm` → publish `clear_alarm`
  - `mulai pomodoro 25` / `pomodoro 25 menit` → publish `start_pomodoro`
  - `stop pomodoro` / `batal pomodoro` → (perlu command MQTT baru, lihat §9)
  - `status` / `cek status` → bridge membaca retained message terakhir di topic status dan membalas ringkasan
  - `matikan alarm` / `silence` → publish `trigger_buzzer:false`
- FR-4: Jika command tidak dikenali, bridge membalas pesan bantuan singkat berisi daftar command yang tersedia.
- FR-5: Command matching bersifat case-insensitive dan toleran terhadap variasi kecil (mis. regex atau keyword matching, bukan exact match).

### 7.3 Notifikasi Otomatis (Push dari Device → WhatsApp)
- FR-6: Bridge subscribe ke topic status. Saat mendeteksi transisi state penting, kirim WhatsApp otomatis ke nomor terdaftar:
  - `gas_level` melewati threshold (naik dari aman → bahaya) → "⚠️ Gas bocor terdeteksi! Level: {gas_level}"
  - `alarm_active` berubah dari `false` → `true` → "⏰ Alarm berbunyi! ({alarm_time})"
  - `pomodoro_running` berubah dari `true` → `false` tanpa dibatalkan manual (selesai natural) → "🍅 Pomodoro selesai! Waktunya istirahat."
  - `online` berubah jadi `false` (device terputus / LWT triggered) → "🔌 Smart Clock terputus dari jaringan."
- FR-7: Notifikasi tidak boleh spam — gunakan debounce/state-diff (bandingkan payload sebelumnya vs sekarang), bukan kirim tiap heartbeat 30 detik.

### 7.4 Balasan Konfirmasi
- FR-8: Setiap command yang berhasil dipublish ke MQTT dibalas konfirmasi ringkas ke WhatsApp (mis. "✅ Alarm diatur ke 06:00"). Bridge tidak menunggu ACK dari device (MQTT QoS di firmware saat ini adalah 1 untuk status, tapi command tidak ada ACK eksplisit) — cukup asumsikan berhasil terkirim, dan biarkan notifikasi status berikutnya (FR-6) yang mengonfirmasi hasil aktual di sisi device.

## 8. Requirement Non-Fungsional

- NFR-1 (Fase 1 — Lokal): Bridge boleh berjalan sebagai proses Node.js biasa di laptop (`node bridge.js` / dikelola PM2 untuk auto-restart). Tidak perlu HA (high availability) di fase ini.
- NFR-2 (Keamanan): Kredensial MQTT dan whitelist nomor WhatsApp disimpan di `.env`, tidak di-hardcode / tidak di-commit ke repo publik.
- NFR-3 (Portabilitas): Semua konfigurasi koneksi (host, port, TLS on/off, kredensial) harus lewat environment variable, bukan hardcoded, agar migrasi ke broker cloud di Fase 2 hanya butuh ubah `.env`.
- NFR-4 (Observability): Bridge mencetak log ke console (dan idealnya ke file) untuk setiap command diterima, dipublish, dan notifikasi dikirim — untuk memudahkan debugging saat pengujian lokal.
- NFR-5 (Resiliency): Bridge harus auto-reconnect ke broker MQTT jika koneksi putus (pola sama seperti `updateMQTT()` di firmware — retry berkala, bukan crash).

## 9. Perubahan yang Dibutuhkan di Firmware (Gap Analysis)

Command MQTT yang **sudah ada** dan bisa langsung dipakai bridge tanpa ubah firmware:
- `set_alarm_time`, `clear_alarm`, `trigger_buzzer`, `start_pomodoro`

Command yang **belum ada** dan perlu ditambahkan ke firmware agar paritas fitur dengan tombol fisik:
- `stop_pomodoro` — untuk membatalkan pomodoro yang sedang berjalan dari WhatsApp (setara PB1 saat `MODE_POMODORO_RUNNING`)
- (Opsional) `get_status` — trigger publish status on-demand, agar command "cek status" dari WhatsApp tidak bergantung pada retained message yang mungkin basi (stale) jika device baru online kembali

> Rekomendasi: tambahkan kedua command ini ke `mqttCallback()` di firmware sebelum development bridge dimulai, supaya bridge tidak perlu workaround.

## 10. Fase & Milestone

### Fase 1 — Lokal (Target: MVP)
1. Setup Mosquitto broker lokal (non-TLS dulu untuk simplifikasi, port 1883) di laptop.
2. Update `mqtt_host`/`mqtt_port` firmware via WiFiManager portal ke IP laptop.
3. Build bridge Node.js: koneksi wwebjs (QR login) + koneksi MQTT client ke broker lokal.
4. Implementasi command parsing dasar (§7.2) dan notifikasi otomatis (§7.3).
5. Tambah `stop_pomodoro` (dan opsional `get_status`) ke firmware.
6. Uji end-to-end: kirim command dari WhatsApp → device bereaksi → notifikasi balik.

### Fase 2 — Migrasi ke MQTT Cloud/Online
1. Pilih provider broker cloud (HiveMQ Cloud free tier / EMQX Cloud / self-hosted VPS + Mosquitto + TLS).
2. Update `.env` bridge (host, port, kredensial, TLS on).
3. Update `mqtt_host`/`mqtt_port`/`mqtt_user`/`mqtt_pass` di firmware via WiFiManager portal (field sudah tersedia di kode saat ini, tidak perlu ubah kode).
4. (Opsional) Pindahkan proses bridge dari laptop ke server kecil (VPS/Raspberry Pi) agar selalu online 24/7, tidak tergantung laptop menyala.
5. Uji ulang end-to-end dari luar jaringan lokal (mis. pakai data seluler di HP).

## 11. Metrik Keberhasilan

- Command WhatsApp direspons device dalam < 3 detik (lokal) selama broker & bridge online.
- Notifikasi gas bocor terkirim ke WhatsApp dalam < 5 detik sejak status berubah di device.
- Tidak ada notifikasi duplikat/spam untuk satu kejadian yang sama (mis. gas bocor terus-menerus tidak mengirim notifikasi tiap 30 detik selama masih dalam kondisi bahaya yang sama).

## 12. Risiko & Mitigasi

| Risiko | Mitigasi |
|---|---|
| Sesi WhatsApp Web (wwebjs) logout/expired sewaktu-waktu | Simpan session via `LocalAuth`, siapkan alert log saat sesi terputus agar user tahu perlu scan ulang QR |
| `whatsapp-web.js` bergantung pada reverse-engineering WhatsApp Web (tidak resmi), berisiko diblokir/berubah sewaktu-waktu oleh WhatsApp | Terima sebagai risiko yang disadari untuk fase lokal/personal; evaluasi migrasi ke WhatsApp Business Cloud API resmi jika stabilitas jadi masalah di fase produksi |
| Laptop mati/sleep membuat bridge & broker lokal offline | Diterima di Fase 1 sebagai batasan; diselesaikan di Fase 2 dengan hosting broker+bridge di server yang selalu nyala |
| Nomor WhatsApp tak dikenal mengirim command (jika whitelist gagal) | Validasi whitelist di awal setiap message handler, sebelum parsing command apapun |

## 13. Open Questions

1. Apakah bridge perlu mendukung lebih dari satu perangkat Smart Clock di masa depan (multi-device via `device_id` berbeda)? Jika ya, command WhatsApp perlu menyebutkan target device.
2. Apakah notifikasi WhatsApp perlu dikirim ke grup, atau selalu ke chat personal admin?
3. Apakah perlu rate-limit command dari WhatsApp (mis. mencegah spam "mulai pomodoro" berkali-kali dalam beberapa detik)?
4. Untuk Fase 2, siapa yang akan menanggung biaya/maintenance broker cloud dan hosting bridge?

## 14. Lampiran: Referensi Command WhatsApp → MQTT Payload

| Perintah WhatsApp (contoh) | Topic MQTT | Payload |
|---|---|---|
| "set alarm 06:00" | `smartclock/{device_id}/command` | `{"set_alarm_time":"06:00"}` |
| "hapus alarm" | `smartclock/{device_id}/command` | `{"clear_alarm":true}` |
| "matikan alarm" | `smartclock/{device_id}/command` | `{"trigger_buzzer":false}` |
| "mulai pomodoro 25" | `smartclock/{device_id}/command` | `{"start_pomodoro":25}` |
| "stop pomodoro" *(butuh tambahan firmware)* | `smartclock/{device_id}/command` | `{"stop_pomodoro":true}` |
| "cek status" | — (baca retained message dari `smartclock/{device_id}/status`, atau publish `{"get_status":true}` jika ditambahkan) | — |