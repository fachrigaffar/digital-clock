const deviceState = require('../store/deviceState');
const db = require('../store/db');
const { generateGasChart } = require('../chart/gasChart');

const DEFAULT_DEVICE_ID = process.env.DEVICE_ID || 'esp32-clock-01';

// FR-4: Help message displayed when command is not recognized
const HELP_MESSAGE =
  `📌 *Daftar Perintah Smart Clock:*\n\n` +
  `• \`set alarm 06:00\` — Atur waktu alarm\n` +
  `• \`hapus alarm\` / \`batal alarm\` — Hapus alarm yang sudah di-set\n` +
  `• \`matikan alarm\` — Hentikan buzzer yang sedang berbunyi\n` +
  `• \`mulai pomodoro 25\` — Mulai timer pomodoro (dalam menit)\n` +
  `• \`stop pomodoro\` / \`batal pomodoro\` — Batalkan pomodoro yang berjalan\n` +
  `• \`status\` / \`cek status\` — Lihat status gas, alarm, dan koneksi\n` +
  `• \`riwayat\` / \`history\` — Lihat 10 riwayat alarm & gas terakhir\n` +
  `• \`grafik\` / \`grafik gas\` — Kirim grafik level gas (30 data terakhir)\n` +
  `• \`bantuan\` / \`help\` — Tampilkan menu ini`;

// FR-5: Time format extractor — supports "06:00", "6 pagi", "18:30", "jam 7 malam"
function extractTime(text) {
  // HH:MM format
  const colonMatch = text.match(/(\d{1,2}):(\d{2})/);
  if (colonMatch) {
    const h = parseInt(colonMatch[1], 10);
    const m = parseInt(colonMatch[2], 10);
    if (h >= 0 && h <= 23 && m >= 0 && m <= 59) {
      return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}`;
    }
  }

  // "6 pagi" / "7 malam" format
  const wordMatch = text.match(/(\d{1,2})\s*(pagi|siang|sore|malam)/i);
  if (wordMatch) {
    let h = parseInt(wordMatch[1], 10);
    const period = wordMatch[2].toLowerCase();
    if (period === 'malam' || period === 'sore') {
      if (h !== 12) h += 12;
      if (h >= 24) h = 0;
    } else if (period === 'pagi') {
      if (h === 12) h = 0;
    }
    return `${String(h).padStart(2, '0')}:00`;
  }

  return null;
}

// FR-5: Minutes extractor for pomodoro
function extractMinutes(text) {
  const match = text.match(/(\d+)\s*(menit|min|m)?/i);
  if (match) {
    const val = parseInt(match[1], 10);
    if (val > 0 && val <= 120) return val;
  }
  return null;
}

/**
 * Handles incoming WhatsApp messages with natural language matching.
 * FR-3, FR-5: Case-insensitive, regex/keyword based matching.
 * @param {string} text
 * @returns {Promise<string|null>}
 */
async function handleCommand(text, senderId = null) {
  if (!text || typeof text !== 'string') return null;

  // Lazy require to avoid circular dependency (mqtt/client → whatsapp/client → commands → mqtt/client)
  const mqttClient = require('../mqtt/client');

  const normalized = text.trim().toLowerCase();

  // FR-3: "status" / "cek status"
  if (/^(status|cek\s*status|lihat\s*status)$/.test(normalized)) {
    const state = deviceState.getState(DEFAULT_DEVICE_ID);
    const onlineStatus = state.online ? '🟢 *Online*' : '🔴 *Offline*';
    const alarmStatus = state.alarm_active ? `🚨 *Aktif* (${state.alarm_time || '-'})` : '⚪ Tidak Aktif';
    const pomodoroStatus = state.pomodoro_running ? '🍅 *Berjalan*' : '⚪ Tidak Aktif';
    const gasLabel = state.gas_level > (parseInt(process.env.GAS_THRESHOLD) || 400)
      ? `⚠️ *${state.gas_level} (BAHAYA!)*`
      : `✅ ${state.gas_level} (Aman)`;

    let lastUpdateStr = 'Belum menerima data';
    if (state.timestamp) {
      const date = new Date(state.timestamp * 1000);
      lastUpdateStr = date.toLocaleString('id-ID', { timeZone: 'Asia/Jakarta' });
    }

    return `📊 *Status Smart Clock* — _${DEFAULT_DEVICE_ID}_\n\n` +
           `• Koneksi  : ${onlineStatus}\n` +
           `• Sensor Gas: ${gasLabel}\n` +
           `• Alarm    : ${alarmStatus}\n` +
           `• Pomodoro : ${pomodoroStatus}\n` +
           `• Update   : ${lastUpdateStr}`;
  }

  // FR-3: "set alarm 06:00" / "set alarm jam 6 pagi"
  if (/set\s+alarm/.test(normalized)) {
    const time = extractTime(text);
    if (!time) {
      return 'Format waktu tidak dikenali. Contoh: "set alarm 06:30" atau "set alarm jam 6 pagi".';
    }
    const success = mqttClient.publishCommand(DEFAULT_DEVICE_ID, { set_alarm_time: time });
    if (success) {
      console.log(`[CMD] set_alarm_time → ${time}`);
      db.logEvent(DEFAULT_DEVICE_ID, 'alarm', `Alarm di-set ke ${time}`);
      return `✅ Alarm diatur ke pukul *${time}*.`;
    }
    return '❌ Gagal mengirim perintah ke broker MQTT.';
  }

  // FR-3: "hapus alarm" / "batal alarm" / "cancel alarm"
  if (/^(hapus|batal|cancel)\s*alarm/.test(normalized)) {
    const success = mqttClient.publishCommand(DEFAULT_DEVICE_ID, { clear_alarm: true });
    if (success) {
      console.log('[CMD] clear_alarm → true');
      return '✅ Alarm telah dihapus.';
    }
    return '❌ Gagal mengirim perintah ke broker MQTT.';
  }

  // FR-3: "matikan alarm" / "silence" / "stop alarm"
  if (/^(matikan|silence|diam|stop)\s*alarm/.test(normalized) || normalized === 'matikan' || normalized === 'silence') {
    const success = mqttClient.publishCommand(DEFAULT_DEVICE_ID, { trigger_buzzer: false });
    if (success) {
      console.log('[CMD] trigger_buzzer → false');
      return '✅ Alarm/buzzer dimatikan.';
    }
    return '❌ Gagal mengirim perintah ke broker MQTT.';
  }

  // FR-3: "mulai pomodoro 25" / "pomodoro 25 menit" / "start pomodoro 25"
  if (/(mulai|start|pomodoro)\s*(pomodoro)?/.test(normalized) && /\d+/.test(normalized)) {
    const minutes = extractMinutes(text);
    if (!minutes) {
      return 'Format tidak dikenali. Contoh: "mulai pomodoro 25" atau "pomodoro 25 menit".';
    }
    const success = mqttClient.publishCommand(DEFAULT_DEVICE_ID, { start_pomodoro: minutes });
    if (success) {
      console.log(`[CMD] start_pomodoro → ${minutes} menit`);
      return `✅ Timer Pomodoro dimulai selama *${minutes} menit*. 🍅`;
    }
    return '❌ Gagal mengirim perintah ke broker MQTT.';
  }

  // FR-3: "stop pomodoro" / "batal pomodoro" / "cancel pomodoro"
  if (/(stop|batal|cancel)\s*pomodoro/.test(normalized)) {
    const success = mqttClient.publishCommand(DEFAULT_DEVICE_ID, { stop_pomodoro: true });
    if (success) {
      console.log('[CMD] stop_pomodoro → true');
      return '✅ Pomodoro dibatalkan.';
    }
    return '❌ Gagal mengirim perintah ke broker MQTT.';
  }

  // FR-3: "bantuan" / "help" / "menu"
  if (/^(bantuan|help|menu|\?|tolong)$/.test(normalized)) {
    return HELP_MESSAGE;
  }

  // Riwayat alarm & gas dari database
  if (/^(riwayat|history|log)$/.test(normalized)) {
    const events = await db.getRecentEvents(10);
    if (!events || events.length === 0) {
      return '📭 Belum ada riwayat alarm atau gas yang tercatat.';
    }
    const lines = events.map((e, i) => {
      const icon = e.event_type === 'alarm' ? '⏰' : '⚠️';
      const time = new Date(e.created_at).toLocaleString('id-ID', { timeZone: 'Asia/Jakarta' });
      return `${i + 1}. ${icon} *${e.event_type.toUpperCase()}* — ${e.detail}\n    🕐 ${time}`;
    });
    return `📋 *10 Riwayat Terakhir:*\n\n${lines.join('\n\n')}`;
  }

  // Grafik level gas
  if (/^(grafik|grafik\s*gas|send\s*grafik|chart\s*gas)$/.test(normalized)) {
    if (!senderId) return '❌ Tidak bisa mengirim grafik.';
    const readings = await db.getRecentGasReadings(DEFAULT_DEVICE_ID, 30);
    if (!readings || readings.length === 0) {
      return '📭 Belum ada data gas yang tercatat. Tunggu beberapa menit agar sensor mengirim data.';
    }
    const imageBuffer = await generateGasChart(readings);
    // Lazy require to avoid circular dependency
    const whatsappClient = require('./client');
    await whatsappClient.sendImage(senderId, imageBuffer, `📊 Grafik Level Gas — ${DEFAULT_DEVICE_ID}\n🕑 ${new Date().toLocaleString('id-ID', { timeZone: 'Asia/Jakarta' })}`);
    return null; // gambar sudah dikirim langsung, tidak perlu reply teks
  }

  // FR-4: Unknown command — reply with help
  return HELP_MESSAGE;
}

module.exports = {
  handleCommand
};
