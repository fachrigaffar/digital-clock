// Load environment variables
require('dotenv').config();

const mqttClient = require('./mqtt/client');
const whatsappClient = require('./whatsapp/client');
const db = require('./store/db');

console.log('=============================================');
console.log('    Smart Digital Clock WhatsApp Gateway      ');
console.log('=============================================');

// Initialize DB, then MQTT and WhatsApp clients
db.init()
  .then(() => {
    mqttClient.init();
    whatsappClient.init();
  })
  .catch((err) => {
    console.error('[DB] Failed to initialize database:', err.message);
    console.warn('[DB] Continuing without database — history command will not work.');
    mqttClient.init();
    whatsappClient.init();
  });

// Handle graceful shutdown
process.on('SIGINT', () => {
  console.log('Shutting down WhatsApp Gateway...');
  const mqtt = mqttClient.getClient();
  if (mqtt) {
    mqtt.end();
  }
  process.exit(0);
});

process.on('unhandledRejection', (reason, promise) => {
  console.error('Unhandled Rejection at:', promise, 'reason:', reason);
});
