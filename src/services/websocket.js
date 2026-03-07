// WebSocket service for C++ backend communication

class WebSocketService {
  constructor() {
    this.ws = null
    this.reconnectTimer = null
    this.reconnectInterval = 3000
    this.messageHandlers = []
    this.statusHandlers = []
  }

  connect(url) {
    return new Promise((resolve, reject) => {
      try {
        this.ws = new WebSocket(url)

        this.ws.onopen = () => {
          console.log('WebSocket connected')
          if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer)
            this.reconnectTimer = null
          }
          this.notifyStatus(true)
          resolve()
        }

        this.ws.onmessage = (event) => {
          this.handleMessage(event.data)
        }

        this.ws.onclose = () => {
          console.log('WebSocket disconnected, attempting to reconnect...')
          this.notifyStatus(false)
          this.reconnectTimer = setTimeout(() => this.connect(url), this.reconnectInterval)
        }

        this.ws.onerror = (error) => {
          console.error('WebSocket error:', error)
          this.notifyStatus(false)
          reject(error)
        }
      } catch (error) {
        this.notifyStatus(false)
        reject(error)
      }
    })
  }

  disconnect() {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer)
      this.reconnectTimer = null
    }
    if (this.ws) {
      this.ws.close()
      this.ws = null
    }
  }

  sendMessage(message) {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(message))
    } else {
      console.error('WebSocket is not connected')
    }
  }

  sendTextMessage(text) {
    this.sendMessage({
      type: 'text',
      content: text,
      timestamp: Date.now()
    })
  }

  sendImageMessage(base64Data) {
    this.sendMessage({
      type: 'image',
      content: base64Data,
      timestamp: Date.now()
    })
  }

  handleMessage(data) {
    try {
      const message = JSON.parse(data)
      this.messageHandlers.forEach(handler => handler(message))
    } catch (error) {
      console.error('Failed to parse message:', error)
    }
  }

  onMessage(handler) {
    this.messageHandlers.push(handler)
  }

  onStatus(handler) {
    this.statusHandlers.push(handler)
    // Immediately notify current status
    handler(this.isConnected())
  }

  notifyStatus(connected) {
    this.statusHandlers.forEach(handler => handler(connected))
  }

  isConnected() {
    return this.ws && this.ws.readyState === WebSocket.OPEN
  }
}

export default new WebSocketService()
