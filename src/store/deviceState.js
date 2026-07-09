const states = {};

/**
 * Gets the current state for a given device ID.
 * If the device has no state, returns a default state with online: false.
 * @param {string} deviceId
 * @returns {object}
 */
function getState(deviceId) {
  return states[deviceId] || {
    device_id: deviceId,
    gas_level: 0,
    alarm_active: false,
    timestamp: 0,
    online: false
  };
}

/**
 * Updates the state for a given device ID.
 * Merges the existing state with the new state.
 * @param {string} deviceId
 * @param {object} newState
 */
function updateState(deviceId, newState) {
  states[deviceId] = {
    ...getState(deviceId),
    ...newState
  };
}

module.exports = {
  getState,
  updateState,
  states
};
