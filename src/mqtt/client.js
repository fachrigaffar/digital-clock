const mqtt = require('mqtt');
const deviceState = require('../store/deviceState');
const whatsappClient = require('../whatsapp/client');
const db = require('../store/db');

const GAS_THRESHOLD = parseInt(process.env.GAS_THRESHOLD || '400', 10);

let client = null;

function init() {
  const host = process.env.MQTT_HOST;
  const port = parseInt(process.env.MQTT_PORT || '8883', 10);
  const username = process.env.MQTT_USER;
  const password = process.env.MQTT_PASS;

  if (!host) {
    console.error('[ERROR] MQTT_HOST is not set in .env');
    return;
  }

  // NFR-3: All connection config from environment variables
  const protocol = port === 8883 ? 'mqtts' : 'mqtt';
  const url = `${protocol}://${host}:${port}`;

  console.log(`[MQTT] Connecting to broker: ${url}...`);

  client = mqtt.connect(url, {
    username,
    password,
    rejectUnauthorized: false, // Allow self-signed certs (cloud brokers use valid certs anyway)
    reconnectPeriod: 5000      // NFR-5: Auto-reconnect every 5 seconds if disconnected
  });

  client.on('connect', () => {
    console.log('[MQTT] Connected to broker successfully.');
    // Subscribe to all device status topics using wildcard
    client.subscribe('smartclock/+/status', { qos: 1 }, (err) => {
      if (err) {
        console.error('[MQTT] Failed to subscribe to status topic:', err);
      } else {
        console.log('[MQTT] Subscribed to: smartclock/+/status');
      }
    });
  });

  client.on('message', (topic, message) => {
    try {
      const topicParts = topic.split('/');
      const deviceId = topicParts[1];
      const payload = JSON.parse(message.toString());

      // NFR-4: Log every received status message
      console.log(`[MQTT] Status from [${deviceId}]:`, payload);

      const isOnline = payload.online !== undefined ? payload.online : true;

      // FR-7: Get previous state BEFORE updating for change detection
      const prev = deviceState.updateState(deviceId, {
        ...payload,
        online: isOnline,
        timestamp: payload.timestamp || Math.floor(Date.now() / 1000)
      });

      // FR-6: Detect state transitions and push WhatsApp notifications
      handleStateTransitions(deviceId, prev, deviceState.getState(deviceId));

    } catch (e) {
      console.error(`[MQTT] Error parsing message on topic ${topic}:`, e.message);
    }
  });

  // NFR-5: Log reconnection events
  client.on('reconnect', () => {
    console.log('[MQTT] Reconnecting to broker...');
  });

  client.on('error', (err) => {
    console.error('[MQTT] Connection error:', err.message);
  });

  client.on('close', () => {
    console.log('[MQTT] Connection closed.');
  });
}

/**
 * FR-6 & FR-7: Detect important state transitions and push WhatsApp notifications.
 * Debounced via state-diff — only fires when state actually CHANGES, not every heartbeat.
 * @param {string} deviceId
 * @param {object} prev - State before the update
 * @param {object} curr - State after the update
 */
function handleStateTransitions(deviceId, prev, curr) {
  // Gas: safe → danger transition only (not on every heartbeat while already dangerous)
  const wasGasSafe = prev.gas_level <= GAS_THRESHOLD;
  const isGasDangerous = curr.gas_level > GAS_THRESHOLD;
  if (wasGasSafe && isGasDangerous) {
    const msg = `⚠️ *Gas bocor terdeteksi!*\nDevice: _${deviceId}_\nLevel gas: *${curr.gas_level}* (ambang batas: ${GAS_THRESHOLD})\nHarap segera periksa!`;
    console.log(`[NOTIFY] Gas danger transition detected for ${deviceId}`);
    whatsappClient.sendNotification(msg);
    db.logEvent(deviceId, 'gas', `Level gas: ${curr.gas_level}`);
  }

  // Alarm: false → true transition only
  if (!prev.alarm_active && curr.alarm_active) {
    const timeLabel = curr.alarm_time ? ` (${curr.alarm_time})` : '';
    const msg = `⏰ *Alarm berbunyi!*${timeLabel}\nDevice: _${deviceId}_`;
    console.log(`[NOTIFY] Alarm activated for ${deviceId}`);
    whatsappClient.sendNotification(msg);
    db.logEvent(deviceId, 'alarm', `Alarm berbunyi${timeLabel}`);
  }

  // Pomodoro: running → stopped (natural finish — user did NOT cancel manually)
  // We detect this when pomodoro_running changes true → false
  // Note: we can't distinguish "done" vs "cancelled" without extra payload field —
  // for now we notify on any true → false transition.
  if (prev.pomodoro_running && !curr.pomodoro_running) {
    const msg = `🍅 *Pomodoro selesai!*\nDevice: _${deviceId}_\nWaktunya istirahat. ☕`;
    console.log(`[NOTIFY] Pomodoro finished for ${deviceId}`);
    whatsappClient.sendNotification(msg);
  }

  // Device: online → offline (LWT triggered)
  if (prev.online && !curr.online) {
    const msg = `🔌 *Smart Clock terputus dari jaringan.*\nDevice: _${deviceId}_\nPastikan perangkat menyala dan terhubung WiFi.`;
    console.log(`[NOTIFY] Device offline detected: ${deviceId}`);
    whatsappClient.sendNotification(msg);
  }
}

/**
 * Publishes a command to the device's command topic.
 * @param {string} deviceId
 * @param {object} command
 * @returns {boolean}
 */
function publishCommand(deviceId, command) {
  if (!client || !client.connected) {
    console.error('[MQTT] Cannot publish — not connected to broker.');
    return false;
  }

  const topic = `smartclock/${deviceId}/command`;
  const payload = JSON.stringify(command);

  // NFR-4: Log every published command
  console.log(`[MQTT] Publish command to [${topic}]:`, command);
  client.publish(topic, payload, { qos: 1, retain: false });
  return true;
}

module.exports = {
  init,
  publishCommand,
  getClient: () => client
};
