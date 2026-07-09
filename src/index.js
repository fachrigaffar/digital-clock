// Load environment variables
require('dotenv').config();

const mqttClient = require('./mqtt/client');
const whatsappClient = require('./whatsapp/client');

console.log('=============================================');
console.log('    Smart Digital Clock WhatsApp Gateway      ');
console.log('=============================================');

// Initialize MQTT client
mqttClient.init();

// Initialize WhatsApp Web client
whatsappClient.init();

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
