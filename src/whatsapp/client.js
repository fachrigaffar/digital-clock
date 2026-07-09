const { Client, LocalAuth } = require('whatsapp-web.js');
const qrcode = require('qrcode-terminal');
const commands = require('./commands');

let client = null;

function init() {
  console.log('Initializing WhatsApp Client...');

  const puppeteerOptions = {
    headless: true,
    args: ['--no-sandbox', '--disable-setuid-sandbox']
  };

  // If Puppeteer download was skipped, allow specifying local executable path via env
  if (process.env.PUPPETEER_EXECUTABLE_PATH) {
    console.log(`Using custom Puppeteer executable path: ${process.env.PUPPETEER_EXECUTABLE_PATH}`);
    puppeteerOptions.executablePath = process.env.PUPPETEER_EXECUTABLE_PATH;
  }

  // Use LocalAuth to persist session (saves session files under .wwebjs_auth/)
  client = new Client({
    authStrategy: new LocalAuth(),
    puppeteer: puppeteerOptions
  });

  client.on('qr', (qr) => {
    console.log('\n--- SCAN THIS QR CODE WITH WHATSAPP TO LOGIN ---');
    qrcode.generate(qr, { small: true });
    console.log('------------------------------------------------\n');
  });

  client.on('ready', () => {
    console.log('WhatsApp Web Client is ready and authenticated!');
  });

  client.on('message', async (msg) => {
    try {
      const reply = await commands.handleCommand(msg.body);
      if (reply) {
        await msg.reply(reply);
      }
    } catch (err) {
      console.error('Error handling message:', err);
    }
  });

  // Handle messages sent by the bot owner to themselves or others
  client.on('message_create', async (msg) => {
    // Only process commands sent by the user (from their own number) if they start with "/"
    if (msg.fromMe) {
      try {
        const reply = await commands.handleCommand(msg.body);
        if (reply) {
          await msg.reply(reply);
        }
      } catch (err) {
        console.error('Error handling self message:', err);
      }
    }
  });

  client.on('auth_failure', (msg) => {
    console.error('WhatsApp Authentication failure:', msg);
  });

  client.on('disconnected', (reason) => {
    console.log('WhatsApp Client was disconnected:', reason);
  });

  client.initialize().catch((err) => {
    console.error('Failed to initialize WhatsApp client:', err);
  });
}

module.exports = {
  init,
  getClient: () => client
};
