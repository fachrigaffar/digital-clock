const mysql = require('mysql2/promise');

let pool = null;

/**
 * Initializes the MySQL connection pool and creates required tables if not exist.
 */
async function init() {
  pool = mysql.createPool({
    host:     process.env.DB_HOST || 'localhost',
    port:     parseInt(process.env.DB_PORT || '3306', 10),
    user:     process.env.DB_USER || 'root',
    password: process.env.DB_PASS || '',
    database: process.env.DB_NAME || 'smartclock',
    waitForConnections: true,
    connectionLimit: 5
  });

  await pool.query(`
    CREATE TABLE IF NOT EXISTS event_log (
      id         INT AUTO_INCREMENT PRIMARY KEY,
      device_id  VARCHAR(64)  NOT NULL,
      event_type ENUM('alarm','gas') NOT NULL,
      detail     VARCHAR(255),
      created_at DATETIME DEFAULT CURRENT_TIMESTAMP
    )
  `);

  await pool.query(`
    CREATE TABLE IF NOT EXISTS gas_readings (
      id         INT AUTO_INCREMENT PRIMARY KEY,
      device_id  VARCHAR(64) NOT NULL,
      gas_level  INT         NOT NULL,
      created_at DATETIME DEFAULT CURRENT_TIMESTAMP
    )
  `);

  console.log('[DB] MySQL connected and tables ready.');
}

/**
 * Inserts a new event record into event_log.
 * @param {string} deviceId
 * @param {'alarm'|'gas'} eventType
 * @param {string} detail
 */
async function logEvent(deviceId, eventType, detail) {
  if (!pool) return;
  try {
    await pool.query(
      'INSERT INTO event_log (device_id, event_type, detail) VALUES (?, ?, ?)',
      [deviceId, eventType, detail]
    );
    console.log(`[DB] Logged event: ${eventType} — ${detail}`);
  } catch (err) {
    console.error('[DB] Failed to log event:', err.message);
  }
}

/**
 * Retrieves the last N events from event_log, newest first.
 * @param {number} limit
 * @returns {Promise<Array>}
 */
async function getRecentEvents(limit = 10) {
  if (!pool) return [];
  try {
    const [rows] = await pool.query(
      'SELECT event_type, detail, device_id, created_at FROM event_log ORDER BY created_at DESC LIMIT ?',
      [limit]
    );
    return rows;
  } catch (err) {
    console.error('[DB] Failed to fetch events:', err.message);
    return [];
  }
}

/**
 * Inserts a gas level reading into gas_readings.
 * @param {string} deviceId
 * @param {number} gasLevel
 */
async function logGasReading(deviceId, gasLevel) {
  if (!pool) return;
  try {
    await pool.query(
      'INSERT INTO gas_readings (device_id, gas_level) VALUES (?, ?)',
      [deviceId, gasLevel]
    );
  } catch (err) {
    console.error('[DB] Failed to log gas reading:', err.message);
  }
}

/**
 * Retrieves the last N gas readings for a device, oldest first (for chart ordering).
 * @param {string} deviceId
 * @param {number} limit
 * @returns {Promise<Array>}
 */
async function getRecentGasReadings(deviceId, limit = 30) {
  if (!pool) return [];
  try {
    const [rows] = await pool.query(
      `SELECT gas_level, created_at FROM (
         SELECT gas_level, created_at FROM gas_readings
         WHERE device_id = ?
         ORDER BY created_at DESC
         LIMIT ?
       ) sub ORDER BY created_at ASC`,
      [deviceId, limit]
    );
    return rows;
  } catch (err) {
    console.error('[DB] Failed to fetch gas readings:', err.message);
    return [];
  }
}

module.exports = { init, logEvent, getRecentEvents, logGasReading, getRecentGasReadings };
