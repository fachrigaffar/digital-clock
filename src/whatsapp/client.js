const {
  default: makeWASocket,
  useMultiFileAuthState,
  DisconnectReason,
  fetchLatestBaileysVersion
} = require('@whiskeysockets/baileys');
const qrcode = require('qrcode-terminal');
const commands = require('./commands');

let sock = null;

// FR-2: Whitelist — format internasional tanpa '+' (mis. 6281234567890)
const ALLOWED_NUMBERS = process.env.ALLOWED_NUMBERS
  ? process.env.ALLOWED_NUMBERS.split(',').map(n => n.trim())
  : [];

function isAllowed(jid) {
  if (ALLOWED_NUMBERS.length === 0) {
    console.warn('[WARN] ALLOWED_NUMBERS tidak di-set — semua pengirim diterima (mode development).');
    return true;
  }
  // Baileys JID format: "6281234567890@s.whatsapp.net"
  const number = jid.split('@')[0];
  return ALLOWED_NUMBERS.includes(number);
}

async function init() {
  console.log('Initializing WhatsApp Client (Baileys — no browser needed)...');

  if (ALLOWED_NUMBERS.length > 0) {
    console.log(`[INFO] Whitelist aktif — nomor diizinkan: ${ALLOWED_NUMBERS.join(', ')}`);
  }

  // Simpan session auth di folder .baileys_auth/ agar tidak perlu scan ulang tiap restart
  const { state, saveCreds } = await useMultiFileAuthState('.baileys_auth');
  const { version } = await fetchLatestBaileysVersion();

  console.log(`[INFO] Menggunakan WhatsApp Web versi: ${version.join('.')}`);

  sock = makeWASocket({
    version,
    auth: state,
    printQRInTerminal: false, // kita handle QR sendiri pakai qrcode-terminal
    logger: {
      // Pino-compatible silent logger — Baileys wajib butuh child() method
      level: 'silent',
      trace: () => {}, debug: () => {}, info: () => {},
      warn: () => {}, error: (msg) => console.error('[BAILEYS]', msg),
      fatal: () => {}, child() { return this; }
    }
  });

  // Event: connection update (QR code, connected, disconnected)
  sock.ev.on('connection.update', async (update) => {
    const { connection, lastDisconnect, qr } = update;

    if (qr) {
      console.log('\n--- SCAN QR CODE INI DENGAN WHATSAPP ---');
      qrcode.generate(qr, { small: true });
      console.log('----------------------------------------\n');
    }

    if (connection === 'open') {
      console.log('[INFO] WhatsApp Client siap dan terautentikasi!');
    }

    if (connection === 'close') {
      const shouldReconnect =
        lastDisconnect?.error?.output?.statusCode !== DisconnectReason.loggedOut;

      if (shouldReconnect) {
        console.log('[WARN] Koneksi WhatsApp terputus, mencoba reconnect...');
        init(); // rekursif reconnect
      } else {
        console.log('[WARN] WhatsApp logout — hapus folder .baileys_auth/ dan jalankan ulang untuk scan QR baru.');
      }
    }
  });

  // Event: simpan credentials tiap kali diperbarui
  sock.ev.on('creds.update', saveCreds);

  // Event: pesan masuk
  sock.ev.on('messages.upsert', async ({ messages, type }) => {
    if (type !== 'notify') return;

    for (const msg of messages) {
      // Abaikan pesan dari diri sendiri dan pesan status broadcast
      if (msg.key.fromMe || msg.key.remoteJid === 'status@broadcast') continue;

      const senderId = msg.key.remoteJid;

      // FR-2: validasi whitelist sebelum parsing command apapun
      if (!isAllowed(senderId)) {
        console.log(`[BLOCKED] Pesan dari nomor tidak diizinkan: ${senderId}`);
        continue;
      }

      const text =
        msg.message?.conversation ||
        msg.message?.extendedTextMessage?.text ||
        '';

      if (!text) continue;

      console.log(`[MSG] Dari ${senderId}: ${text}`);

      try {
        const reply = await commands.handleCommand(text, senderId);
        if (reply) {
          await sock.sendMessage(senderId, { text: reply });
        }
      } catch (err) {
        console.error('[ERROR] Gagal memproses pesan:', err.message);
      }
    }
  });
}

/**
 * Kirim notifikasi push ke nomor pertama di whitelist.
 * @param {string} message
 */
async function sendNotification(message) {
  if (!sock) return;
  if (ALLOWED_NUMBERS.length === 0) {
    console.warn('[WARN] Tidak bisa kirim notifikasi — ALLOWED_NUMBERS tidak di-set.');
    return;
  }
  const target = `${ALLOWED_NUMBERS[0]}@s.whatsapp.net`;
  try {
    await sock.sendMessage(target, { text: message });
    console.log(`[NOTIFY] Terkirim ke ${target}: ${message}`);
  } catch (err) {
    console.error('[ERROR] Gagal kirim notifikasi:', err.message);
  }
}

/**
 * Kirim gambar (buffer PNG) ke JID tertentu.
 * @param {string} jid - WhatsApp JID penerima
 * @param {Buffer} imageBuffer
 * @param {string} caption
 */
async function sendImage(jid, imageBuffer, caption = '') {
  if (!sock) return;
  try {
    await sock.sendMessage(jid, { image: imageBuffer, caption });
    console.log(`[NOTIFY] Gambar terkirim ke ${jid}`);
  } catch (err) {
    console.error('[ERROR] Gagal kirim gambar:', err.message);
  }
}

module.exports = {
  init,
  sendNotification,
  sendImage,
  getClient: () => sock
};
