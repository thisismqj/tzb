<template>
  <div class="chat-container">
    <!-- Header -->
    <div class="chat-header">
      <div class="header-info">
        <div class="avatar bot-avatar">AI</div>
        <div class="bot-info">
          <h3>AI Assistant</h3>
          <span class="status" :class="{ online: isConnected }">
            {{ isConnected ? 'Online' : 'Offline' }}
          </span>
        </div>
      </div>
    </div>

    <!-- Message List -->
    <div class="message-list" ref="messageListRef">
      <div
        v-for="(message, index) in messages"
        :key="index"
        class="message-item"
        :class="{ 'user-message': message.isUser, 'bot-message': !message.isUser }"
      >
        <div class="message-avatar" :class="{ 'user-avatar': message.isUser }">
          {{ message.isUser ? 'You' : 'AI' }}
        </div>
        <div class="message-content">
          <!-- Text Message -->
          <div v-if="message.type === 'text'" class="text-message" v-html="renderMarkdown(message.content)"></div>
          <!-- Image Message -->
          <div v-else-if="message.type === 'image'" class="image-message">
            <img :src="message.content" alt="Image" @click="previewImage(message.content)" />
          </div>
          <div class="message-time">
            {{ formatTime(message.timestamp) }}
          </div>
        </div>
      </div>

      <!-- Typing Indicator -->
      <div v-if="isTyping" class="message-item bot-message">
        <div class="message-avatar">AI</div>
        <div class="message-content">
          <div class="typing-indicator">
            <span></span>
            <span></span>
            <span></span>
          </div>
        </div>
      </div>
    </div>

    <!-- Input Area -->
    <div class="input-area">
      <!-- Model Selector -->
      <div class="model-selector">
        <label for="model-select">选择模型：</label>
        <select id="model-select" v-model="selectedModel">
          <option value="deepseek">DeepSeek</option>
          <option value="doubao">DouBao</option>
        </select>
      </div>

      <!-- Image Preview -->
      <div v-if="imagePreview" class="image-preview">
        <img :src="imagePreview" alt="Preview" />
        <button class="remove-image" @click="removeImage">×</button>
      </div>

      <div class="input-wrapper">
        <label class="image-upload-btn" title="Send image">
          <input
            type="file"
            accept="image/*"
            @change="handleImageUpload"
            ref="imageInputRef"
          />
          <svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <rect x="3" y="3" width="18" height="18" rx="2" ry="2"/>
            <circle cx="8.5" cy="8.5" r="1.5"/>
            <polyline points="21 15 16 10 5 21"/>
          </svg>
        </label>

        <input
          v-model="inputText"
          type="text"
          class="text-input"
          placeholder="Type a message..."
          @keyup.enter="sendMessage"
          :disabled="!isConnected"
        />

        <button
          class="send-btn"
          @click="sendMessage"
          :disabled="(!inputText.trim() && !imagePreview) || !isConnected"
        >
          <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <line x1="22" y1="2" x2="11" y2="13"/>
            <polygon points="22 2 15 22 11 13 2 9 22 2"/>
          </svg>
        </button>
      </div>
    </div>

    <!-- Image Preview Modal -->
    <div v-if="previewUrl" class="modal-overlay" @click="previewUrl = null">
      <div class="modal-content">
        <img :src="previewUrl" alt="Preview" />
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted, nextTick } from 'vue'
import websocketService from '../services/websocket'
import MarkdownIt from 'markdown-it'

// 创建markdown实例
const md = new MarkdownIt({
  html: true,
  linkify: true,
  typographer: true
})

// 渲染markdown函数
const renderMarkdown = (content) => {
  return md.render(content)
}

// State
const messages = ref([])
const inputText = ref('')
const isConnected = ref(false)
const isTyping = ref(false)
const imagePreview = ref(null)
const previewUrl = ref(null)
const messageListRef = ref(null)
const imageInputRef = ref(null)
const selectedModel = ref('deepseek') // 默认模型

// WebSocket URL - update to match your C++ backend
const WS_URL = 'ws://localhost:8080'

// Scroll to bottom
const scrollToBottom = async () => {
  await nextTick()
  if (messageListRef.value) {
    messageListRef.value.scrollTop = messageListRef.value.scrollHeight
  }
}

// Format time
const formatTime = (timestamp) => {
  const date = new Date(timestamp)
  return date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' })
}

// Handle image upload
const handleImageUpload = (event) => {
  const file = event.target.files[0]
  if (file) {
    const reader = new FileReader()
    reader.onload = (e) => {
      imagePreview.value = e.target.result
    }
    reader.readAsDataURL(file)
  }
  event.target.value = ''
}

// Remove image
const removeImage = () => {
  imagePreview.value = null
}

// Send message
const sendMessage = () => {
  const hasText = inputText.value.trim()
  const hasImage = imagePreview.value

  // Don't send if both text and image are empty
  if (!hasText && !hasImage) {
    return
  }

  // Send text only
  if (hasText && !hasImage) {
    const message = {
      id: Date.now(),
      type: 'text',
      content: inputText.value.trim(),
      model: selectedModel.value,
      timestamp: Date.now(),
      isUser: true
    }
    messages.value.push(message)
    websocketService.sendTextMessage(message.content, selectedModel.value)
    inputText.value = ''
    scrollToBottom()
    return
  }

  // Send image only
  if (!hasText && hasImage) {
    const message = {
      id: Date.now(),
      type: 'image',
      content: imagePreview.value,
      model: selectedModel.value,
      timestamp: Date.now(),
      isUser: true
    }
    messages.value.push(message)
    websocketService.sendImageMessage(imagePreview.value, selectedModel.value)
    imagePreview.value = null
    scrollToBottom()
    return
  }

  // Send both text and image (as separate messages)
  if (hasText && hasImage) {
    // Send text message
    const textMessage = {
      id: Date.now(),
      type: 'text',
      content: inputText.value.trim(),
      model: selectedModel.value,
      timestamp: Date.now(),
      isUser: true
    }
    messages.value.push(textMessage)
    websocketService.sendTextMessage(textMessage.content, selectedModel.value)
    inputText.value = ''

    // Send image message
    const imageMessage = {
      id: Date.now() + 1,
      type: 'image',
      content: imagePreview.value,
      model: selectedModel.value,
      timestamp: Date.now(),
      isUser: true
    }
    messages.value.push(imageMessage)
    websocketService.sendImageMessage(imagePreview.value, selectedModel.value)
    imagePreview.value = null

    scrollToBottom()
  }
}

// Preview image
const previewImage = (url) => {
  previewUrl.value = url
}

// Connect to WebSocket
const connectWebSocket = async () => {
  try {
    await websocketService.connect(WS_URL)
  } catch (error) {
    console.error('Failed to connect:', error)
  }
}

// Handle connection status
websocketService.onStatus((connected) => {
  isConnected.value = connected
})

// Handle incoming messages
websocketService.onMessage((message) => {
  if (message.type === 'typing') {
    isTyping.value = message.status
  } else {
    messages.value.push({
      ...message,
      isUser: false
    })
    scrollToBottom()
  }
})

// Lifecycle
onMounted(() => {
  connectWebSocket()
})

onUnmounted(() => {
  websocketService.disconnect()
})
</script>

<style scoped>
.chat-container {
  display: flex;
  flex-direction: column;
  height: 100vh;
  width: 100%;
  max-width: 100%;
  background: #f5f5f5;
  box-sizing: border-box;
}

/* Header */
.chat-header {
  background: #667eea;
  color: white;
  padding: clamp(12px, 2vw, 20px) clamp(16px, 3vw, 24px);
  box-shadow: 0 2px 10px rgba(0, 0, 0, 0.1);
  flex-shrink: 0;
}

.header-info {
  display: flex;
  align-items: center;
  gap: clamp(8px, 1.5vw, 12px);
}

.avatar {
  width: clamp(36px, 5vw, 45px);
  height: clamp(36px, 5vw, 45px);
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  font-weight: bold;
  font-size: clamp(12px, 2vw, 16px);
  flex-shrink: 0;
}

.bot-avatar {
  background: white;
  color: #667eea;
}

.bot-info h3 {
  margin: 0;
  font-size: clamp(16px, 2.5vw, 18px);
  font-weight: 600;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.status {
  font-size: clamp(11px, 2vw, 13px);
  opacity: 0.9;
}

.status.online {
  color: #90EE90;
}

/* Message List */
.message-list {
  flex: 1;
  overflow-y: auto;
  padding: clamp(12px, 2vw, 20px);
  display: flex;
  flex-direction: column;
  gap: clamp(12px, 2vw, 16px);
  min-height: 0;
}

.message-item {
  display: flex;
  align-items: flex-end;
  gap: clamp(8px, 1.5vw, 10px);
  max-width: clamp(70%, 85vw, 85%);
}

.user-message {
  align-self: flex-end;
  flex-direction: row-reverse;
}

.message-avatar {
  width: clamp(30px, 4vw, 35px);
  height: clamp(30px, 4vw, 35px);
  border-radius: 50%;
  background: #667eea;
  color: white;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: clamp(10px, 2vw, 12px);
  font-weight: bold;
  flex-shrink: 0;
}

.user-avatar {
  background: #764ba2;
}

.message-content {
  background: white;
  padding: clamp(10px, 2vw, 14px) clamp(12px, 2.5vw, 18px);
  border-radius: 18px;
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.1);
  max-width: 100%;
  word-wrap: break-word;
}

.text-message {
  word-wrap: break-word;
  line-height: 1.5;
  font-size: clamp(13px, 2vw, 15px);
  color: #000;
}

/* Markdown styles */
.text-message h1,
.text-message h2,
.text-message h3,
.text-message h4,
.text-message h5,
.text-message h6 {
  margin-top: 1em;
  margin-bottom: 0.5em;
  font-weight: bold;
  line-height: 1.2;
}

.text-message h1 { font-size: 1.5em; }
.text-message h2 { font-size: 1.3em; }
.text-message h3 { font-size: 1.1em; }

.text-message p {
  margin: 0.5em 0;
}

.text-message ul,
.text-message ol {
  margin: 0.5em 0;
  padding-left: 1.5em;
}

.text-message li {
  margin: 0.25em 0;
}

.text-message code {
  background-color: #f4f4f4;
  padding: 2px 4px;
  border-radius: 3px;
  font-family: 'Monaco', 'Menlo', 'Ubuntu Mono', monospace;
  font-size: 0.9em;
}

.text-message pre {
  background-color: #f4f4f4;
  padding: 1em;
  border-radius: 6px;
  overflow-x: auto;
  margin: 0.5em 0;
}

.text-message pre code {
  background: none;
  padding: 0;
  border-radius: 0;
}

.text-message blockquote {
  border-left: 4px solid #ddd;
  padding-left: 1em;
  margin: 0.5em 0;
  color: #666;
  font-style: italic;
}

.text-message a {
  color: #667eea;
  text-decoration: none;
}

.text-message a:hover {
  text-decoration: underline;
}

.text-message strong,
.text-message b {
  font-weight: bold;
}

.text-message em,
.text-message i {
  font-style: italic;
}

.text-message table {
  border-collapse: collapse;
  margin: 0.5em 0;
  width: 100%;
}

.text-message th,
.text-message td {
  border: 1px solid #ddd;
  padding: 6px 12px;
  text-align: left;
}

.text-message th {
  background-color: #f8f8f8;
  font-weight: bold;
}

.image-message img {
  width: 100%;
  max-width: clamp(150px, 40vw, 280px);
  max-height: clamp(150px, 40vw, 280px);
  height: auto;
  border-radius: 12px;
  cursor: pointer;
  transition: transform 0.2s;
  display: block;
}

.image-message img:hover {
  transform: scale(1.02);
}

.message-time {
  font-size: clamp(10px, 1.8vw, 11px);
  opacity: 0.7;
  margin-top: 6px;
  text-align: right;
}

/* Typing Indicator */
.typing-indicator {
  display: flex;
  gap: 4px;
  padding: 8px 0;
}

.typing-indicator span {
  width: 8px;
  height: 8px;
  background: #667eea;
  border-radius: 50%;
  animation: typing 1.4s infinite;
}

.typing-indicator span:nth-child(2) {
  animation-delay: 0.2s;
}

.typing-indicator span:nth-child(3) {
  animation-delay: 0.4s;
}

@keyframes typing {
  0%, 60%, 100% {
    transform: translateY(0);
  }
  30% {
    transform: translateY(-10px);
  }
}

/* Input Area */
.input-area {
  background: white;
  padding: clamp(12px, 2vw, 20px);
  border-top: 1px solid #e0e0e0;
  flex-shrink: 0;
  width: 100%;
}

.model-selector {
  margin-bottom: 10px;
  display: flex;
  align-items: center;
  gap: 10px;
}

.model-selector label {
  font-size: 14px;
  font-weight: bold;
}

.model-selector select {
  padding: 5px 10px;
  border: 1px solid #ccc;
  border-radius: 4px;
  font-size: 14px;
}

.image-preview {
  position: relative;
  display: inline-block;
  margin-bottom: 10px;
  max-width: 100%;
}

.image-preview img {
  max-height: clamp(80px, 15vw, 120px);
  max-width: 100%;
  width: auto;
  border-radius: 8px;
  border: 2px solid #667eea;
}

.remove-image {
  position: absolute;
  top: -6px;
  right: -6px;
  width: 20px;
  height: 20px;
  min-width: 20px;
  min-height: 20px;
  border-radius: 50%;
  background: #ff4444;
  color: white;
  border: none;
  cursor: pointer;
  font-size: 14px;
  line-height: 1;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 0;
  flex-shrink: 0;
}

.input-wrapper {
  display: flex;
  align-items: center;
  gap: clamp(8px, 1.5vw, 12px);
  width: 100%;
}

.image-upload-btn {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 40px;
  height: 40px;
  min-width: 40px;
  min-height: 40px;
  border-radius: 50%;
  background: #f0f0f0;
  cursor: pointer;
  transition: background 0.2s;
  color: #667eea;
  flex-shrink: 0;
}

.image-upload-btn:hover {
  background: #e0e0e0;
}

.image-upload-btn input {
  display: none;
}

.image-upload-btn svg {
  width: 20px;
  height: 20px;
}

.text-input {
  flex: 1;
  padding: clamp(10px, 2vw, 14px) clamp(14px, 2.5vw, 20px);
  border: 2px solid #e0e0e0;
  border-radius: 25px;
  font-size: clamp(13px, 2vw, 15px);
  outline: none;
  transition: border-color 0.2s;
  min-width: 0;
  min-height: 40px;
}

.text-input:focus {
  border-color: #667eea;
}

.text-input:disabled {
  background: #f5f5f5;
}

.send-btn {
  width: 40px;
  height: 40px;
  min-width: 40px;
  min-height: 40px;
  border-radius: 50%;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  color: white;
  border: none;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: transform 0.2s, opacity 0.2s;
  flex-shrink: 0;
}

.send-btn svg {
  width: 20px;
  height: 20px;
}

.send-btn:hover:not(:disabled) {
  transform: scale(1.1);
}

.send-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

/* Modal */
.modal-overlay {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: rgba(0, 0, 0, 0.8);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
  cursor: pointer;
  padding: 20px;
}

.modal-content {
  max-width: 100%;
  max-height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
}

.modal-content img {
  max-width: 100%;
  max-height: 90vh;
  width: auto;
  height: auto;
  border-radius: 8px;
  object-fit: contain;
}
</style>
