const mqtt = require('mqtt');
const deviceState = require('../store/deviceState');

let client = null;

function init() {
  const host = process.env.MQTT_HOST;
  const port = parseInt(process.env.MQTT_PORT || '8883', 10);
  const username = process.env.MQTT_USER;
  const password = process.env.MQTT_PASS;

  if (!host) {
    console.error('MQTT_HOST is not set in environment variables.');
    return;
  }

  const protocol = port === 8883 ? 'mqtts' : 'mqtt';
  const url = `${protocol}://${host}:${port}`;

  console.log(`Connecting to MQTT Broker: ${url}...`);

  client = mqtt.connect(url, {
    username,
    password,
    rejectUnauthorized: false // Avoid SSL certificate validation issues on some platforms
  });

  client.on('connect', () => {
    console.log('Connected to MQTT Broker successfully.');
    // Subscribe to status of all devices using wildcard
    client.subscribe('smartclock/+/status', { qos: 1 }, (err) => {
      if (err) {
        console.error('Failed to subscribe to status topic:', err);
      } else {
        console.log('Subscribed to status topic: smartclock/+/status');
      }
    });
  });

  client.on('message', (topic, message) => {
    try {
      const topicParts = topic.split('/');
      const deviceId = topicParts[1];
      const payload = JSON.parse(message.toString());

      console.log(`Received message on topic [${topic}]:`, payload);

      // If the payload specifies online status (like LWT), use it.
      // Otherwise, if we received a regular status report, the device is online.
      const isOnline = payload.online !== undefined ? payload.online : true;

      deviceState.updateState(deviceId, {
        ...payload,
        online: isOnline,
        timestamp: payload.timestamp || Math.floor(Date.now() / 1000)
      });
    } catch (e) {
      console.error(`Error parsing message on topic ${topic}:`, e.message);
    }
  });

  client.on('error', (err) => {
    console.error('MQTT client connection error:', err);
  });

  client.on('close', () => {
    console.log('MQTT connection closed.');
  });
}

/**
 * Publishes a command to the device's command topic.
 * @param {string} deviceId
 * @param {object} command
 * @returns {boolean}
 */
function publishCommand(deviceId, command) {
  if (!client || !client.connected) {
    console.error('MQTT client is not connected. Cannot publish command.');
    return false;
  }

  const topic = `smartclock/${deviceId}/command`;
  const payload = JSON.stringify(command);

  console.log(`Publishing command to [${topic}]:`, command);
  client.publish(topic, payload, { qos: 1, retain: false });
  return true;
}

module.exports = {
  init,
  publishCommand,
  getClient: () => client
};
