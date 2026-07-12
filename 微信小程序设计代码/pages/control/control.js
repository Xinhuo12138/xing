// pages/control/control.js
const onenet = require('../../utils/onenet')
const command = require('../../utils/command')

const STORAGE_KEY = 'mushroom_device_states'

Page({
  data: {
    connected: false,
    lastUpdateTime: '--',
    sending: false,
    cmdPreview: '',
    uploadStatus: '', uploadTime: '', uploadDevice: '', uploadMsg: '',

    devices: [
      { id: 'fan', name: '风扇', icon: '🌀', category: '通风系统', state: false, cloudState: null, speed: 50, mode: 0 },
      { id: 'pump', name: '水泵', icon: '💦', category: '灌溉系统', state: false, cloudState: null, mode: 0 },
      { id: 'led', name: 'LED补光灯', icon: '💡', category: '光照系统', state: false, cloudState: null, brightness: 80, mode: 0 },
      { id: 'atomizer', name: '加湿器', icon: '💨', category: '湿度控制', state: false, cloudState: null, mode: 0 },
      { id: 'audio', name: '音频播放', icon: '🔊', category: '声音系统', state: false, cloudState: null, mode: 0 },
      { id: 'buzzer', name: '蜂鸣器', icon: '🔔', category: '报警系统', state: false, cloudState: null, mode: 0 },
    ]
  },

  onLoad() {
    this.loadLocalState()
    this.loadDeviceStatus()
  },

  onShow() {
    this.loadLocalState()
    this.loadDeviceStatus()
  },

  loadLocalState() {
    try {
      const saved = wx.getStorageSync(STORAGE_KEY)
      if (saved && Array.isArray(saved)) {
        const d = [...this.data.devices]
        saved.forEach(s => {
          const idx = d.findIndex(x => x.id === s.id)
          if (idx !== -1) {
            Object.keys(s).forEach(k => {
              if (k !== 'id' && k !== 'name' && k !== 'icon' && k !== 'category' && k !== 'service')
                d[idx][k] = s[k]
            })
          }
        })
        this.setData({ devices: d })
      }
    } catch (e) {}
  },

  saveLocalState() {
    try {
      const data = this.data.devices.map(d => ({
        id: d.id, state: d.state, speed: d.speed, brightness: d.brightness, mode: d.mode
      }))
      wx.setStorageSync(STORAGE_KEY, data)
    } catch (e) {}
  },

  loadDeviceStatus() {
    onenet.queryDeviceProperty()
      .then(result => {
        if (result.statusCode !== 200 || !result.data || result.data.code !== 0) {
          this.setData({ connected: false })
          return
        }
        const rawArr = result.data.data || []
        let data = {}
        if (Array.isArray(rawArr)) {
          rawArr.forEach(item => { if (item.identifier) data[item.identifier] = item.value })
        } else { data = rawArr }

        const now = new Date()
        const ts = `${String(now.getHours()).padStart(2,'0')}:${String(now.getMinutes()).padStart(2,'0')}:${String(now.getSeconds()).padStart(2,'0')}`
        const updates = { connected: true, lastUpdateTime: ts }
        const d = [...this.data.devices]
        const get = (k) => { const v = data[k]; return (v && typeof v === 'object' && 'value' in v) ? v.value : (v ?? null) }

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

        updates.devices = d
        this.setData(updates)
        this.saveLocalState()
      })
      .catch(() => this.setData({ connected: false }))
  },

  onSliderChanging(e) {
    const id = e.currentTarget.dataset.id
    const val = e.detail.value
    this.setData({ [`_sliderDisplay_${id}`]: val })
  },

  onSliderChange(e) {
    const id = e.currentTarget.dataset.id
    const idx = this.data.devices.findIndex(d => d.id === id)
    if (idx === -1) return
    const device = this.data.devices[idx]
    const key = id === 'fan' ? 'speed' : 'brightness'
    const val = e.detail.value
    const wasOn = device.state

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
        this.saveLocalState()
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

  /** 全局定时器回调 */
  onTimerRefresh() {
    this.loadDeviceStatus()
  },

  onBack() { wx.navigateBack() }
})
