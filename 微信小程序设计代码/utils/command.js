/**
 * 命令构建器 v5 - 构建 call-service 服务调用参数
 * 服务标识: command
 * 参数: type(1-6), state(0/1), mode(0/1), value(0-100)
 */

const DEVICE_TYPE = {
  fan: 1, pump: 2, led: 3, buzzer: 4, atomizer: 5, audio: 6,
}

function buildCommand(deviceId, params) {
  const type = DEVICE_TYPE[deviceId]
  if (!type) return null

  const status = params.status !== undefined ? params.status : false
  const mode = params.mode !== undefined ? params.mode : 0

  let value = 0
  if (deviceId === 'fan') value = params.speed || 0
  if (deviceId === 'led') value = params.brightness || 0

  return {
    type: type,
    state: status ? 1 : 0,
    mode: mode,
    value: value
  }
}

module.exports = {
  buildCommand,
  DEVICE_TYPE
}