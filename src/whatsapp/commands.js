const deviceState = require('../store/deviceState');
const mqttClient = require('../mqtt/client');

const DEFAULT_DEVICE_ID = process.env.DEVICE_ID || 'esp32-clock-01';

/**
 * Handles incoming WhatsApp commands.
 * @param {string} text The incoming message text
 * @returns {Promise<string|null>} The reply message, or null if not a command
 */
async function handleCommand(text) {
  if (!text || !text.startsWith('/')) {
    return null;
  }

  const tokens = text.trim().split(/\s+/);
  const command = tokens[0].toLowerCase();
  const args = tokens.slice(1);

  switch (command) {
    case '/alarm': {
      if (args.length < 1) {
        return 'Format salah. Gunakan: `/alarm HH:MM` (contoh: `/alarm 06:30`)';
      }
      const timeVal = args[0];
      // Basic time regex validation (HH:MM)
      const timeRegex = /^([0-1]?[0-9]|2[0-3]):[0-5][0-9]$/;
      if (!timeRegex.test(timeVal)) {
        return 'Format waktu tidak valid. Harap gunakan format HH:MM (contoh: 06:30)';
      }

      const success = mqttClient.publishCommand(DEFAULT_DEVICE_ID, {
        set_alarm_time: timeVal
      });

      if (success) {
        return `Berhasil mengatur alarm ke pukul *${timeVal}* untuk device *${DEFAULT_DEVICE_ID}*.`;
      } else {
        return 'Gagal mengirim perintah alarm ke broker MQTT. Pastikan server terhubung ke broker.';
      }
    }

    case '/matikan': {
      const success = mqttClient.publishCommand(DEFAULT_DEVICE_ID, {
        trigger_buzzer: false
      });

      if (success) {
        return `Perintah mematikan alarm (buzzer) telah dikirim ke device *${DEFAULT_DEVICE_ID}*.`;
      } else {
        return 'Gagal mengirim perintah ke broker MQTT. Pastikan server terhubung ke broker.';
      }
    }

    case '/status': {
      const state = deviceState.getState(DEFAULT_DEVICE_ID);
      const onlineStatus = state.online ? '🟢 *Online*' : '🔴 *Offline*';
      const alarmStatus = state.alarm_active ? '🚨 *Aktif (Buzzer Bunyi)*' : '⚪ Tidak Aktif';
      
      let lastUpdateStr = 'Belum menerima data';
      if (state.timestamp) {
        const date = new Date(state.timestamp * 1000);
        lastUpdateStr = date.toLocaleString('id-ID', { timeZone: 'Asia/Jakarta' });
      }

      return `📊 *Status Device:* _${DEFAULT_DEVICE_ID}_\n\n` +
             `• Koneksi: ${onlineStatus}\n` +
             `• Sensor Gas: *${state.gas_level !== undefined ? state.gas_level : 'N/A'}*\n` +
             `• Alarm State: ${alarmStatus}\n` +
             `• Terakhir Diupdate: ${lastUpdateStr}`;
    }

    case '/help':
    case '/menu': {
      return `📌 *Daftar Perintah Bot WhatsApp:*\n\n` +
             `• \`/status\` - Melihat status sensor gas, alarm, dan koneksi device.\n` +
             `• \`/alarm HH:MM\` - Mengatur waktu alarm (contoh: \`/alarm 06:30\`).\n` +
             `• \`/matikan\` - Mematikan alarm/buzzer secara manual.\n` +
             `• \`/help\` - Menampilkan menu bantuan ini.`;
    }

    default:
      return `Perintah tidak dikenal. Ketik \`/help\` untuk melihat daftar perintah.`;
  }
}

module.exports = {
  handleCommand
};
