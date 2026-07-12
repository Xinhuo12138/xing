// pages/index/index.js
const onenet = require('../../utils/onenet')
const command = require('../../utils/command')

const WEATHER_MAP = {
  0:  { text: '晴', icon: '☀️' },  1:  { text: '晴（夜间）', icon: '🌙' },
  2:  { text: '晴', icon: '☀️' },  3:  { text: '晴（夜间）', icon: '🌙' },
  4:  { text: '多云', icon: '⛅' },  5:  { text: '晴间多云', icon: '🌤️' },
  6:  { text: '晴间多云', icon: '🌤️' },  7:  { text: '大部多云', icon: '⛅' },
  8:  { text: '大部多云', icon: '⛅' },  9:  { text: '阴', icon: '☁️' },
  10: { text: '阵雨', icon: '🌦️' }, 11: { text: '雷阵雨', icon: '⛈️' },
  12: { text: '雷阵雨伴有冰雹', icon: '⛈️' }, 13: { text: '小雨', icon: '🌦️' },
  14: { text: '中雨', icon: '🌧️' }, 15: { text: '大雨', icon: '🌧️' },
  16: { text: '暴雨', icon: '🌧️' }, 17: { text: '大暴雨', icon: '🌧️' },
  18: { text: '特大暴雨', icon: '🌧️' }, 19: { text: '冻雨', icon: '🌧️' },
  20: { text: '雨夹雪', icon: '🌨️' }, 21: { text: '阵雪', icon: '🌨️' },
  22: { text: '小雪', icon: '❄️' },  23: { text: '中雪', icon: '❄️' },
  24: { text: '大雪', icon: '❄️' },  25: { text: '暴雪', icon: '❄️' },
  26: { text: '浮尘', icon: '🌫️' }, 27: { text: '扬沙', icon: '🌫️' },
  28: { text: '沙尘暴', icon: '🌫️' }, 29: { text: '强沙尘暴', icon: '🌫️' },
  30: { text: '雾', icon: '🌫️' },   31: { text: '霾', icon: '😶‍🌫️' },
  32: { text: '风', icon: '💨' },    33: { text: '大风', icon: '💨' },
  34: { text: '飓风', icon: '🌀' },  35: { text: '热带风暴', icon: '🌀' },
  36: { text: '龙卷风', icon: '🌪️' }, 37: { text: '冷', icon: '🥶' },
  38: { text: '热', icon: '🥵' },
}

// 蘑菇品种名称（从云平台获取序号后映射为文字）
const MUSHROOM_NAMES = ['未知（默认）', '红菇', '蓝菇', '绿菇', '云菇', '黄菇']

// 每种蘑菇对应环境目标参数（与 envData key 对应）
const MUSHROOM_ENV = {
  0: { air_temperature: { min:14, max:30 }, air_humidity: { min:65, max:95 }, soil_temperature: { min:12, max:28 }, soil_moisture: { min:55, max:85 }, light_intensity: { min:0, max:3000 }, co2_concentration: { min:400, max:1000 }, tvoc_concentration: { min:0, max:100 } },
  1: { air_temperature: { min:26, max:30 }, air_humidity: { min:85, max:95 }, soil_temperature: { min:24, max:28 }, soil_moisture: { min:75, max:85 }, light_intensity: { min:400, max:2500 }, co2_concentration: { min:400, max:1000 }, tvoc_concentration: { min:0, max:100 } },
  2: { air_temperature: { min:22, max:26 }, air_humidity: { min:80, max:92 }, soil_temperature: { min:20, max:24 }, soil_moisture: { min:70, max:82 }, light_intensity: { min:1500, max:3000 }, co2_concentration: { min:400, max:700 }, tvoc_concentration: { min:0, max:100 } },
  3: { air_temperature: { min:20, max:24 }, air_humidity: { min:75, max:88 }, soil_temperature: { min:18, max:22 }, soil_moisture: { min:65, max:78 }, light_intensity: { min:750, max:1500 }, co2_concentration: { min:400, max:800 }, tvoc_concentration: { min:0, max:100 } },
  4: { air_temperature: { min:14, max:18 }, air_humidity: { min:65, max:78 }, soil_temperature: { min:12, max:16 }, soil_moisture: { min:55, max:68 }, light_intensity: { min:150, max:300 }, co2_concentration: { min:400, max:550 }, tvoc_concentration: { min:0, max:100 } },
  5: { air_temperature: { min:21, max:25 }, air_humidity: { min:78, max:90 }, soil_temperature: { min:19, max:23 }, soil_moisture: { min:68, max:80 }, light_intensity: { min:50, max:350 }, co2_concentration: { min:400, max:850 }, tvoc_concentration: { min:0, max:100 } },
}

Page({
  data: {
    activeTab: 0,
    tabList: ['环境数据', '天气信息', '执行器控制'],

    connected: false, lastUpdateTime: '--', refreshing: false,
    sending: false, cmdPreview: '',
    uploadStatus: '', uploadTime: '', uploadDevice: '', uploadMsg: '',

    debugMode: true, debugHttpStatus: '', debugRawResponse: '', debugError: '',

    envData: {
      air_temperature: { label: '空气温度', value: '--', unit: '°C', icon: '🌡️', status: 'normal', range: '18~25°C' },
      air_humidity: { label: '空气湿度', value: '--', unit: '%RH', icon: '💧', status: 'normal', range: '80~95%' },
      soil_temperature: { label: '土壤温度', value: '--', unit: '°C', icon: '🌍', status: 'normal', range: '16~22°C' },
      soil_moisture: { label: '土壤湿度', value: '--', unit: '%', icon: '🌱', status: 'normal', range: '60~80%' },
      co2_concentration: { label: 'CO₂浓度', value: '--', unit: 'ppm', icon: '🌬️', status: 'normal', range: '400~1500' },
      light_intensity: { label: '光照强度', value: '--', unit: 'Lux', icon: '☀️', status: 'normal', range: '0~500' },
      tvoc_concentration: { label: 'TVOC浓度', value: '--', unit: 'ppb', icon: '🧪', status: 'normal', range: '0~200' },
      mushroom_type: { label: '蘑菇种类', value: '--', unit: '', icon: '🍄', status: 'normal' },
    },

    weatherData: [
      { day: '今天', date: '--', icon: '🌤️', text: '--', high: '--', low: '--', windDir: '--', windScale: '--' },
      { day: '明天', date: '--', icon: '🌤️', text: '--', high: '--', low: '--', windDir: '--', windScale: '--' },
      { day: '后天', date: '--', icon: '🌤️', text: '--', high: '--', low: '--', windDir: '--', windScale: '--' },
    ],

    // 执行器控制（完整设备数据，含滑块、模式）
    devices: [
      { id: 'fan', name: '风扇', icon: '🌀', category: '通风系统', state: false, cloudState: null, speed: 50, mode: 0 },
      { id: 'pump', name: '水泵', icon: '💦', category: '灌溉系统', state: false, cloudState: null, mode: 0 },
      { id: 'led', name: 'LED补光灯', icon: '💡', category: '光照系统', state: false, cloudState: null, brightness: 80, mode: 0 },
      { id: 'atomizer', name: '加湿器', icon: '💨', category: '湿度控制', state: false, cloudState: null, mode: 0 },
      { id: 'audio', name: '音频播放', icon: '🔊', category: '声音系统', state: false, cloudState: null, mode: 0, timeSlots: ['08:30-11:30', '14:00-17:00'] },
      { id: 'buzzer', name: '蜂鸣器', icon: '🔔', category: '报警系统', state: false, cloudState: null, mode: 0 },
    ],
  },

  onLoad() { this.loadAllData() },
  onShow() { this.loadAllData() },
  onPullDownRefresh() { this.loadAllData().finally(() => wx.stopPullDownRefresh()) },

  // ===================== 数据加载 =====================

  loadAllData() {
    this.setData({ refreshing: true, debugError: '' })
    return onenet.queryDeviceProperty()
      .then(result => {
        const httpStatus = result.statusCode
        this.setData({ debugHttpStatus: `${httpStatus}`, debugRawResponse: JSON.stringify(result.data, null, 2) })

        if (httpStatus === 200 && result.data && result.data.code === 0) {
          const rawData = result.data.data || {}
          const now = new Date()
          const ts = `${String(now.getHours()).padStart(2,'0')}:${String(now.getMinutes()).padStart(2,'0')}:${String(now.getSeconds()).padStart(2,'0')}`
          this.setData({ connected: true, lastUpdateTime: ts, refreshing: false })

          this.parseEnvData(rawData)
          this.parseWeatherData(rawData)
          this.parseDeviceData(rawData)
        } else {
          const msg = result.data ? (result.data.msg || JSON.stringify(result.data)) : '空响应'
          this.setData({ connected: false, refreshing: false, debugError: `HTTP ${httpStatus} | ${msg}` })
        }
      })
      .catch(err => {
        this.setData({ connected: false, refreshing: false, debugError: err.errMsg || '请求失败' })
      })
  },

  normalizeArrayData(arr) {
    if (!Array.isArray(arr)) return arr
    const obj = {}
    arr.forEach(item => { if (item.identifier !== undefined) obj[item.identifier] = item.value })
    return obj
  },

  getStatus(val, min, max) {
    let offset = (max - min) * 0.15
    if (offset < 1) offset = 1

    if (val < min - offset || val > max + offset) return 'danger'
    if (val < min || val > max) return 'warning'
    return 'normal'
  },

  parseEnvData(data) {
    if (Array.isArray(data)) data = this.normalizeArrayData(data)
    const env = { ...this.data.envData }

    const getVal = (k) => {
      const v = data[k]
      if (v === undefined || v === null) return null
      if (typeof v === 'object' && v !== null && 'value' in v) return v.value
      return v
    }

    // 读取蘑菇种类序号（0-5），映射为名称并切换到对应环境阈值
    let mushroomIdx = parseInt(getVal('mushroom_type'))
    if (isNaN(mushroomIdx) || mushroomIdx < 0 || mushroomIdx > 5) mushroomIdx = 0
    const mushroomName = MUSHROOM_NAMES[mushroomIdx] || '未设置'
    env.mushroom_type.value = mushroomName

    // 根据蘑菇品种选择阈值
    const thresholds = MUSHROOM_ENV[mushroomIdx] || MUSHROOM_ENV[0]

    Object.keys(env).forEach(key => {
      if (key === 'mushroom_type') return
      const val = getVal(key)
      if (val === null) return
      const num = parseFloat(val)
      env[key].value = !isNaN(num) ? (num % 1 === 0 ? String(val) : num.toFixed(1)) : String(val)
      if (thresholds[key]) {
        const { min, max } = thresholds[key]
        env[key].status = this.getStatus(num, min, max)
        env[key].range = `${min}~${max}${env[key].unit}`
      }
    })
    this.setData({ envData: env, liveThresholds: thresholds })
  },

  parseWeatherData(data) {
    if (Array.isArray(data)) data = this.normalizeArrayData(data)
    const weather = [...this.data.weatherData]
    ;['1','2','3'].forEach((s, i) => {
      const get = (k) => { const v = data[k]; return (v && typeof v === 'object' && 'value' in v) ? v.value : (v ?? null) }
      const date = get(`weather_date${s}`), code = get(`weather_code_day${s}`)
      const high = get(`weather_high${s}`), low = get(`weather_low${s}`)
      const wd = get(`weather_wind_dir${s}`), ws = get(`weather_wind_scale${s}`)
      const wi = (code !== null && WEATHER_MAP[code]) || { text: '--', icon: '🌤️' }
      weather[i] = { day: ['今天','明天','后天'][i], date: date || '--', icon: wi.icon, text: wi.text,
        high: high !== null ? high+'°C' : '--', low: low !== null ? low+'°C' : '--',
        windDir: wd || '--', windScale: ws || '--' }
    })
    this.setData({ weatherData: weather })
  },

  /** 解析设备状态（从OneNET更新，只写cloudState不覆盖用户操作） */
  parseDeviceData(data) {
    if (Array.isArray(data)) data = this.normalizeArrayData(data)
    const get = (k) => { const v = data[k]; return (v && typeof v === 'object' && 'value' in v) ? v.value : (v ?? null) }
    const d = [...this.data.devices]

    const fs = get('fan_state'), fsp = get('fan_speed'), fm = get('fan_work_mode')
    if (fs !== null) { d[0].cloudState = Boolean(Number(fs)); d[0].cloudSpeed = fsp !== null ? Number(fsp) : null; d[0].cloudMode = fm ?? null }

    const ps = get('pump_state'), pm = get('pump_work_mode')
    if (ps !== null) { d[1].cloudState = Boolean(Number(ps)); d[1].cloudMode = pm ?? null }

    const ls = get('led_state'), lb = get('led_brightness'), lm = get('led_work_mode')
    if (ls !== null) { d[2].cloudState = Boolean(Number(ls)); d[2].cloudBrightness = lb !== null ? Number(lb) : null; d[2].cloudMode = lm ?? null }

    const as = get('atomizer_state'), am = get('atomizer_work_mode')
    if (as !== null) { d[3].cloudState = Boolean(Number(as)); d[3].cloudMode = am ?? null }

    const aus = get('audio_state'), aum = get('audio_work_mode')
    if (aus !== null) { d[4].cloudState = Boolean(Number(aus)); d[4].cloudMode = aum ?? null }

    const bs = get('buzzer_state')
    if (bs !== null) { d[5].cloudState = Boolean(Number(bs)) }

    this.setData({ devices: d })
  },

  // ===================== 控制逻辑 =====================

  onSliderChanging(e) {
    // 拖动时只更新显示数值，不写回devices（避免setData干扰原生slider）
    const id = e.currentTarget.dataset.id
    const val = e.detail.value
    const key = id === 'fan' ? 'speed' : 'brightness'
    this.setData({ [`_sliderDisplay_${id}`]: val })
  },

  onSliderChange(e) {
    const id = e.currentTarget.dataset.id
    const idx = this.data.devices.findIndex(d => d.id === id)
    if (idx === -1) return
    const device = this.data.devices[idx]
    const key = id === 'fan' ? 'speed' : 'brightness'
    const val = e.detail.value  // 用 e.detail.value 获取最终值
    const wasOn = device.state

    // 更新实际数值并切为手动模式
    this.setData({ [`devices[${idx}].${key}`]: val, [`devices[${idx}].mode`]: 0, [`_sliderDisplay_${id}`]: val })

    if (val === 0) {
      this.setData({ [`devices[${idx}].state`]: false })
      this.sendCommand(device, false)
    } else {
      this.setData({ [`devices[${idx}].state`]: true })
      this.sendCommand(device, true, wasOn)
    }
  },

  onModeChange(e) {
    const id = e.currentTarget.dataset.id
    const mode = parseInt(e.currentTarget.dataset.mode)
    if (this.data.sending) return
    const idx = this.data.devices.findIndex(d => d.id === id)
    if (idx === -1) return
    this.setData({ [`devices[${idx}].mode`]: mode })
    this.sendCommand(this.data.devices[idx], this.data.devices[idx].state, false, true)
  },

  onDeviceToggle(e) {
    const deviceId = e.currentTarget.dataset.id
    if (!deviceId || this.data.sending) return
    const idx = this.data.devices.findIndex(d => d.id === deviceId)
    if (idx === -1) return
    // 开关操作自动切为手动模式
    const newStatus = !this.data.devices[idx].state
    this.setData({ [`devices[${idx}].mode`]: 0 })
    this.sendCommand(this.data.devices[idx], newStatus)
  },

  sendCommand(device, status, adjusting, modeChange) {
    this.setData({ sending: true, uploadStatus: 'sending' })
    const params = { status, speed: device.speed ?? 0, brightness: device.brightness ?? 0, mode: device.mode || 0 }
    const cmdProps = command.buildCommand(device.id, params)
    if (!cmdProps) {
      this.setData({ sending: false, uploadStatus: 'fail', uploadMsg: '属性构建失败' })
      wx.showToast({ title: '属性构建失败', icon: 'none' })
      return
    }

    const now = new Date()
    const ts = `${String(now.getHours()).padStart(2,'0')}:${String(now.getMinutes()).padStart(2,'0')}:${String(now.getSeconds()).padStart(2,'0')}`
    this.setData({
      cmdPreview: JSON.stringify(cmdProps, null, 2),
      uploadTime: ts,
      uploadDevice: device.name
    })

    // 通过 call-service 调用命令服务
    console.log('[Cmd] 调用服务:', JSON.stringify(cmdProps))
    onenet.callService('command', cmdProps).then(res => {
      console.log('[Cmd] 服务调用响应:', JSON.stringify(res))
      if (res.statusCode === 200 && res.data && res.data.code === 0) {
        const idx = this.data.devices.findIndex(d => d.id === device.id)
        if (idx !== -1) this.setData({ [`devices[${idx}].state`]: status })
        this.setData({ sending: false, uploadStatus: 'success' })
        const txt = modeChange ? '切换成功' : (!status ? '关闭成功' : (adjusting ? '调整成功' : '开启成功'))
        wx.showToast({ title: txt, icon: 'success' })
      } else {
        const errMsg = res.data ? (res.data.msg || JSON.stringify(res.data)) : `HTTP ${res.statusCode}`
        this.setData({ sending: false, uploadStatus: 'fail', uploadMsg: `API错误: ${errMsg}` })
        wx.showToast({ title: '指令下发失败', icon: 'none' })
        console.error('[Cmd] API返回错误:', errMsg)
      }
    }).catch(err => {
      console.error('[Cmd] 请求异常:', err.errMsg || err)
      this.setData({ sending: false, uploadStatus: 'fail', uploadMsg: `网络错误: ${err.errMsg || '请求失败'}` })
      wx.showToast({ title: '操作失败', icon: 'none' })
    })
  },

  // ===================== 页面交互 =====================

  onTabSwitch(e) { this.setData({ activeTab: e.currentTarget.dataset.index }) },
  onSwiperChange(e) { this.setData({ activeTab: e.detail.current }) },
  toggleDebug() { this.setData({ debugMode: !this.data.debugMode }) },
  /** 全局定时器回调 - 静默刷新数据 */
  onTimerRefresh() {
    onenet.queryDeviceProperty()
      .then(result => {
        if (result.statusCode === 200 && result.data && result.data.code === 0) {
          const rawData = result.data.data || {}
          const now = new Date()
          const ts = `${String(now.getHours()).padStart(2,'0')}:${String(now.getMinutes()).padStart(2,'0')}:${String(now.getSeconds()).padStart(2,'0')}`
          this.setData({ connected: true, lastUpdateTime: ts })
          this.parseEnvData(rawData)
          this.parseWeatherData(rawData)
          this.parseDeviceData(rawData)
        }
      })
      .catch(() => {})
  },

  onRefresh() { wx.startPullDownRefresh() },
})
