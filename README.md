# Vue Chat Application

一个人机聊天界面，支持收发图片，使用 WebSocket 与 C++ 后端通信。

## 功能特性

- ✅ 实时聊天（WebSocket）
- ✅ 发送/接收文本消息
- ✅ 发送/接收图片（Base64 编码）
- ✅ 打字状态指示
- ✅ 连接状态显示
- ✅ 图片预览
- ✅ 现代化 UI 设计

## 前端运行

```bash
# 安装依赖
npm install

# 开发模式运行
npm run dev
```

访问 `http://localhost:5173`

## 后端编译 (C++)

### 依赖

- CMake >= 3.10
- Boost
- [websocketpp](https://github.com/zaphoyd/websocketpp)
- [jsoncpp](https://github.com/open-source-parsers/jsoncpp)

### 编译

```bash
cd cpp-backend

# 安装依赖 (Ubuntu/Debian)
sudo apt-get install libboost-all-dev libjsoncpp-dev

# 创建构建目录
mkdir build && cd build

# 配置和编译
cmake ..
make

# 运行服务器
./chat_server
```

## WebSocket 消息格式

### 客户端发送

**文本消息:**
```json
{
  "type": "text",
  "content": "Hello",
  "timestamp": 1234567890
}
```

**图片消息:**
```json
{
  "type": "image",
  "content": "data:image/png;base64,...",
  "timestamp": 1234567890
}
```

### 服务端发送

**文本回复:**
```json
{
  "type": "text",
  "content": "Response message",
  "timestamp": 1234567890
}
```

**打字状态:**
```json
{
  "type": "typing",
  "status": true
}
```

## 项目结构

```
mychat/
├── src/
│   ├── components/
│   │   └── ChatComponent.vue    # 聊天界面组件
│   ├── services/
│   │   └── websocket.js         # WebSocket 服务
│   └── App.vue                  # 主应用
├── cpp-backend/
│   ├── main.cpp                 # C++ 服务器
│   └── CMakeLists.txt           # 构建配置
└── package.json
```

## 配置

修改 `src/components/ChatComponent.vue` 中的 WebSocket URL:

```javascript
const WS_URL = 'ws://localhost:8080'  // 改为你的服务器地址
```
