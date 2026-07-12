// app.js
App({
  onLaunch() {
    console.log('[App] 蘑菇智慧种植小程序启动')
    this.globalData.startTime = Date.now()
    // 全局定时刷新（每10秒，各页面自行决定是否响应）
    this.globalData.refreshTimer = setInterval(() => {
      const pages = getCurrentPages()
      const p = pages[pages.length - 1]
      if (p && typeof p.onTimerRefresh === 'function') {
        p.onTimerRefresh()
      }
    }, 10000)
  },

  onHide() {
    if (this.globalData.refreshTimer) {
      clearInterval(this.globalData.refreshTimer)
      this.globalData.refreshTimer = null
    }
  },

  onShow() {
    if (!this.globalData.refreshTimer) {
      this.globalData.refreshTimer = setInterval(() => {
        const pages = getCurrentPages()
        const p = pages[pages.length - 1]
        if (p && typeof p.onTimerRefresh === 'function') {
          p.onTimerRefresh()
        }
      }, 10000)
    }
  },

  globalData: {
    deviceConnected: false,
    startTime: null,
    refreshTimer: null,
    // 环境阈值参考
    thresholds: {
      air_temperature: { min: 18, max: 25, unit: '°C', label: '空气温度' },
      air_humidity: { min: 80, max: 95, unit: '%RH', label: '空气湿度' },
      soil_temperature: { min: 16, max: 22, unit: '°C', label: '土壤温度' },
      soil_moisture: { min: 60, max: 80, unit: '%', label: '土壤湿度' },
      co2_concentration: { min: 400, max: 1500, unit: 'ppm', label: 'CO₂浓度' },
      light_intensity: { min: 0, max: 500, unit: 'Lux', label: '光照强度' },
      tvoc_concentration: { min: 0, max: 200, unit: 'ppb', label: 'TVOC浓度' },
    }
  }
})
