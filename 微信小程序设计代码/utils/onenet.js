/**
 * OneNET 云平台 API 工具模块 v8
 * 使用 call-service 调用设备服务
 * 文档：/thingmodel/call-service (POST)
 */

const API_HOST = 'https://iot-api.heclouds.com'
const PRODUCT_ID = '      '
const DEVICE_NAME = '     '
const TOKEN = ''

function getHeaders() {
  return {
    'authorization': TOKEN,
    'Content-Type': 'application/json'
  }
}

function queryDeviceProperty() {
  return new Promise((resolve, reject) => {
    wx.request({
      url: `${API_HOST}/thingmodel/query-device-property`,
      method: 'GET',
      header: getHeaders(),
      data: { product_id: PRODUCT_ID, device_name: DEVICE_NAME },
      success: (res) => {
        console.log('[OneNET] 查询属性:', JSON.stringify(res.data, null, 2))
        resolve({ statusCode: res.statusCode, data: res.data, raw: res })
      },
      fail: (err) => reject({ type: 'network', errMsg: err.errMsg, raw: err })
    })
  })
}

/**
 * 调用设备服务（call-service）
 * identifier: 服务标识（如 'command'）
 * params: 服务输入参数 { type, state, mode, value }
 */
function callService(identifier, params) {
  return new Promise((resolve, reject) => {
    const body = {
      product_id: PRODUCT_ID,
      device_name: DEVICE_NAME,
      identifier: identifier,
      params: params
    }
    console.log('[OneNET] 调用服务:', JSON.stringify(body, null, 2))
    wx.request({
      url: `${API_HOST}/thingmodel/call-service`,
      method: 'POST',
      header: getHeaders(),
      data: body,
      success: (res) => {
        console.log('[OneNET] 服务调用响应:', res.statusCode, JSON.stringify(res.data, null, 2))
        resolve({ statusCode: res.statusCode, data: res.data, raw: res })
      },
      fail: (err) => {
        console.error('[OneNET] 服务调用网络失败:', err.errMsg)
        reject({ type: 'network', errMsg: err.errMsg, raw: err })
      }
    })
  })
}

module.exports = {
  queryDeviceProperty,
  callService
}