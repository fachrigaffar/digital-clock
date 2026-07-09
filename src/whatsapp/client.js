const { Client, LocalAuth } = require('whatsapp-web.js');
const qrcode = require('qrcode-terminal');
const commands = require('./commands');

let client = null;

// FR-2: Whitelist of allowed numbers. Stored in .env as comma-separated string.
// Format: international format without '+', e.g. "6281234567890,6289876543210"
const ALLOWED_NUMBERS = process.env.ALLOWED_NUMBERS
  ? process.env.ALLOWED_NUMBERS.split(',').map(n => n.trim())
  : [];

/**
 * Checks if a sender is allowed to issue commands.
 * If ALLOWED_NUMBERS is empty, all senders are allowed (development mode).
 */
function isAllowed(senderId) {
  if (ALLOWED_NUMBERS.length === 0) {
    console.warn('[WARN] ALLOWED_NUMBERS not set — accepting commands from everyone. Set it in .env to restrict access.');
    return true;
  }
  // senderId from wwebjs is in format "628xxx@c.us"
  const number = senderId.replace('@c.us', '').replace('@s.whatsapp.net', '');
  return ALLOWED_NUMBERS.includes(number);
}

function init() {
  console.log('Initializing WhatsApp Client...');

  if (ALLOWED_NUMBERS.length > 0) {
    console.log(`[INFO] Whitelist active — allowed numbers: ${ALLOWED_NUMBERS.join(', ')}`);
  }

  const puppeteerOptions = {
    headless: true,
    args: ['--no-sandbox', '--disable-setuid-sandbox']
  };

  if (process.env.PUPPETEER_EXECUTABLE_PATH) {
    console.log(`[INFO] Using custom Puppeteer executable: ${process.env.PUPPETEER_EXECUTABLE_PATH}`);
    puppeteerOptions.executablePath = process.env.PUPPETEER_EXECUTABLE_PATH;
  }

  // FR-1: Use LocalAuth to persist session — no re-scan after restart
  client = new Client({
    authStrategy: new LocalAuth(),
    puppeteer: puppeteerOptions
  });

  client.on('qr', (qr) => {
    console.log('\n--- SCAN QR CODE INI DENGAN WHATSAPP ---');
    qrcode.generate(qr, { small: true });
    console.log('----------------------------------------\n');
  });

  client.on('ready', () => {
    console.log('[INFO] WhatsApp Client siap dan terautentikasi!');
  });

  client.on('message', async (msg) => {
    // FR-2: Validate sender against whitelist before parsing any command
    if (!isAllowed(msg.from)) {
      console.log(`[BLOCKED] Message from unauthorized number: ${msg.from}`);
      return;
    }
    try {
      console.log(`[MSG] From ${msg.from}: ${msg.body}`);
      const reply = await commands.handleCommand(msg.body);
      if (reply) {
        await msg.reply(reply);
      }
    } catch (err) {
      console.error('[ERROR] Error handling message:', err);
    }
  });

  client.on('auth_failure', (msg) => {
    console.error('[ERROR] WhatsApp authentication failure:', msg);
  });

  client.on('disconnected', (reason) => {
    console.log('[WARN] WhatsApp Client disconnected:', reason);
  });

  client.initialize().catch((err) => {
    console.error('[ERROR] Failed to initialize WhatsApp client:', err);
  });
}

/**
 * Sends a message to the first allowed number as a push notification.
 * @param {string} message
 */
async function sendNotification(message) {
  if (!client) return;
  if (ALLOWED_NUMBERS.length === 0) {
    console.warn('[WARN] Cannot send push notification — ALLOWED_NUMBERS is not set in .env');
    return;
  }
  const target = `${ALLOWED_NUMBERS[0]}@c.us`;
  try {
    await client.sendMessage(target, message);
    console.log(`[NOTIFY] Sent to ${target}: ${message}`);
  } catch (err) {
    console.error('[ERROR] Failed to send notification:', err.message);
  }
}

module.exports = {
  init,
  sendNotification,
  getClient: () => client
};
