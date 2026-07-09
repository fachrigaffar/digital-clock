const states = {};

// Default state structure matching the PRD §6 payload contract
const DEFAULT_STATE = {
  device_id: '',
  gas_level: 0,
  alarm_active: false,
  alarm_time: null,
  pomodoro_running: false,
  online: false,
  timestamp: 0
};

/**
 * Gets the current state for a given device ID.
 * Returns a default state if no data has been received yet.
 * @param {string} deviceId
 * @returns {object}
 */
function getState(deviceId) {
  return states[deviceId] || { ...DEFAULT_STATE, device_id: deviceId };
}

/**
 * Updates the state for a given device ID by merging with existing state.
 * Returns the previous state before merging (used for change detection / debounce).
 * @param {string} deviceId
 * @param {object} newState
 * @returns {object} previousState
 */
function updateState(deviceId, newState) {
  const prev = getState(deviceId);
  states[deviceId] = {
    ...prev,
    ...newState
  };
  return prev;
}

module.exports = {
  getState,
  updateState,
  states
};
