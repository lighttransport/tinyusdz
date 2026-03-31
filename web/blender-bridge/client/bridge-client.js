// Blender Bridge WebSocket Client for Browser
// Connects to Blender Bridge server and handles scene/parameter updates

/**
 * Message Types (must match server)
 */
export const MessageType = {
  SCENE_UPLOAD: 'scene_upload',
  PARAMETER_UPDATE: 'parameter_update',
  ACK: 'ack',
  STATUS: 'status',
  ERROR: 'error',
  PING: 'ping',
  PONG: 'pong',
  SESSION_CREATED: 'session_created',
  EXPORT_REQUEST: 'export_request',
  EXECUTE_CODE: 'execute_code',
  EXPORT_STARTED: 'export_started',
  EXPORT_PROGRESS: 'export_progress',
  CODE_RESULT: 'code_result'
};

/**
 * Decode message from ArrayBuffer
 * Format: [4 bytes header length (LE)] + [JSON header] + [binary payload]
 */
function decodeMessage(data) {
  const buffer = data instanceof ArrayBuffer ? data : data.buffer;
  const view = new DataView(buffer);

  // Read header length (little-endian)
  const headerLength = view.getUint32(0, true);

  // Read header
  const headerBytes = new Uint8Array(buffer, 4, headerLength);
  const headerJson = new TextDecoder().decode(headerBytes);
  const header = JSON.parse(headerJson);

  // Read payload if present
  const payloadOffset = 4 + headerLength;
  const payloadLength = buffer.byteLength - payloadOffset;
  const payload = payloadLength > 0 ?
    new Uint8Array(buffer, payloadOffset, payloadLength) :
    null;

  return { header, payload };
}

/**
 * Encode message with optional binary payload
 */
function encodeMessage(header, payload = null) {
  if (!header.messageId) {
    header.messageId = crypto.randomUUID();
  }
  if (!header.timestamp) {
    header.timestamp = Date.now();
  }

  const headerJson = JSON.stringify(header);
  const headerBytes = new TextEncoder().encode(headerJson);
  const headerLength = headerBytes.length;

  const payloadBytes = payload ?
    (payload instanceof Uint8Array ? payload : new Uint8Array(payload)) :
    new Uint8Array(0);

  const totalSize = 4 + headerLength + payloadBytes.length;
  const result = new ArrayBuffer(totalSize);
  const view = new DataView(result);

  view.setUint32(0, headerLength, true);
  new Uint8Array(result, 4, headerLength).set(headerBytes);

  if (payloadBytes.length > 0) {
    new Uint8Array(result, 4 + headerLength).set(payloadBytes);
  }

  return result;
}

/**
 * BridgeClient - WebSocket client for Blender Bridge
 */
export class BridgeClient extends EventTarget {
  constructor(options = {}) {
    super();

    this.serverUrl = options.serverUrl || 'ws://localhost:8090';
    this.sessionId = options.sessionId;
    this.autoReconnect = options.autoReconnect !== false;
    this.reconnectDelay = options.reconnectDelay || 2000;
    this.maxReconnectAttempts = options.maxReconnectAttempts || 10;

    this.ws = null;
    this.connected = false;
    this.reconnectAttempts = 0;
    this._reconnectTimer = null;

    // Callbacks for specific message types
    this.onSceneUpload = null;
    this.onParameterUpdate = null;
    this.onError = null;
  }

  /**
   * Connect to the Blender Bridge server
   *
   * @param {string} [sessionId] - Session ID to join
   * @returns {Promise<void>}
   */
  connect(sessionId = this.sessionId) {
    return new Promise((resolve, reject) => {
      if (this.connected) {
        resolve();
        return;
      }

      this.sessionId = sessionId;

      const url = new URL(this.serverUrl);
      url.searchParams.set('type', 'browser');
      if (sessionId) {
        url.searchParams.set('session', sessionId);
      }

      console.log(`Connecting to Blender Bridge: ${url}`);

      this.ws = new WebSocket(url);
      this.ws.binaryType = 'arraybuffer';

      this.ws.onopen = () => {
        console.log('Connected to Blender Bridge');
        this.connected = true;
        this.reconnectAttempts = 0;
        this.dispatchEvent(new CustomEvent('connected'));
        resolve();
      };

      this.ws.onclose = (event) => {
        console.log(`Disconnected from Blender Bridge: code=${event.code}`);
        this.connected = false;
        this.dispatchEvent(new CustomEvent('disconnected', {
          detail: { code: event.code, reason: event.reason }
        }));

        // Auto reconnect
        if (this.autoReconnect && this.reconnectAttempts < this.maxReconnectAttempts) {
          this._scheduleReconnect();
        }
      };

      this.ws.onerror = (error) => {
        console.error('WebSocket error:', error);
        this.dispatchEvent(new CustomEvent('error', { detail: error }));
        reject(error);
      };

      this.ws.onmessage = (event) => {
        this._handleMessage(event.data);
      };
    });
  }

  /**
   * Disconnect from server
   */
  disconnect() {
    this.autoReconnect = false;
    if (this._reconnectTimer) {
      clearTimeout(this._reconnectTimer);
      this._reconnectTimer = null;
    }
    if (this.ws) {
      this.ws.close(1000, 'Client disconnect');
      this.ws = null;
    }
    this.connected = false;
  }

  /**
   * Schedule reconnection attempt
   */
  _scheduleReconnect() {
    if (this._reconnectTimer) return;

    this.reconnectAttempts++;
    const delay = this.reconnectDelay * Math.min(this.reconnectAttempts, 5);

    console.log(`Reconnecting in ${delay}ms (attempt ${this.reconnectAttempts}/${this.maxReconnectAttempts})`);

    this._reconnectTimer = setTimeout(() => {
      this._reconnectTimer = null;
      this.connect(this.sessionId).catch(err => {
        console.error('Reconnect failed:', err);
      });
    }, delay);
  }

  /**
   * Handle incoming message
   */
  _handleMessage(data) {
    try {
      const { header, payload } = decodeMessage(data);

      switch (header.type) {
        case MessageType.SCENE_UPLOAD:
          this._handleSceneUpload(header, payload);
          break;

        case MessageType.PARAMETER_UPDATE:
          this._handleParameterUpdate(header);
          break;

        case MessageType.PING:
          this._sendPong(header.messageId);
          break;

        case MessageType.ERROR:
          console.error('Server error:', header.error);
          this.dispatchEvent(new CustomEvent('server-error', { detail: header.error }));
          if (this.onError) {
            this.onError(header.error);
          }
          break;

        case MessageType.SESSION_CREATED:
          console.log('Session created:', header.sessionId);
          this.sessionId = header.sessionId;
          this.dispatchEvent(new CustomEvent('session-created', {
            detail: { sessionId: header.sessionId }
          }));
          break;

        case MessageType.EXPORT_STARTED:
          console.log('Export started');
          this.dispatchEvent(new CustomEvent('export-started', {
            detail: { refMessageId: header.refMessageId }
          }));
          break;

        case MessageType.EXPORT_PROGRESS:
          this.dispatchEvent(new CustomEvent('export-progress', {
            detail: { progress: header.progress, refMessageId: header.refMessageId }
          }));
          break;

        case MessageType.CODE_RESULT:
          console.log('Code result:', header.success ? 'success' : 'failed');
          this.dispatchEvent(new CustomEvent('code-result', {
            detail: {
              success: header.success,
              output: header.output,
              error: header.error,
              refMessageId: header.refMessageId
            }
          }));
          break;

        default:
          console.log('Unknown message type:', header.type);
      }
    } catch (err) {
      console.error('Message handling error:', err);
    }
  }

  /**
   * Handle scene upload message
   */
  _handleSceneUpload(header, payload) {
    console.log('Scene received:', header.scene);

    const sceneData = {
      header: header,
      binaryData: payload,
      scene: header.scene,
      metadata: header.metadata
    };

    this.dispatchEvent(new CustomEvent('scene-upload', { detail: sceneData }));

    if (this.onSceneUpload) {
      this.onSceneUpload(sceneData);
    }

    // Send acknowledgment
    this._sendAck(header.messageId, 'success');
  }

  /**
   * Handle parameter update message
   */
  _handleParameterUpdate(header) {
    const updateData = {
      target: header.target,
      changes: header.changes
    };

    this.dispatchEvent(new CustomEvent('parameter-update', { detail: updateData }));

    if (this.onParameterUpdate) {
      this.onParameterUpdate(updateData);
    }

    // Send acknowledgment
    this._sendAck(header.messageId, 'success');
  }

  /**
   * Send acknowledgment
   */
  _sendAck(refMessageId, status) {
    if (!this.connected || !this.ws) return;

    const message = encodeMessage({
      type: MessageType.ACK,
      refMessageId: refMessageId,
      status: status
    });

    this.ws.send(message);
  }

  /**
   * Send pong response
   */
  _sendPong(refMessageId) {
    if (!this.connected || !this.ws) return;

    const message = encodeMessage({
      type: MessageType.PONG,
      refMessageId: refMessageId
    });

    this.ws.send(message);
  }

  /**
   * Send status update to server
   *
   * @param {Object} viewerState - Current viewer state
   */
  sendStatus(viewerState) {
    if (!this.connected || !this.ws) return;

    const message = encodeMessage({
      type: MessageType.STATUS,
      viewer: viewerState
    });

    this.ws.send(message);
  }

  /**
   * Send error to server
   *
   * @param {string} code - Error code
   * @param {string} message - Error message
   * @param {Object} [details] - Additional details
   */
  sendError(code, message, details = {}) {
    if (!this.connected || !this.ws) return;

    const msg = encodeMessage({
      type: MessageType.ERROR,
      error: { code, message, details }
    });

    this.ws.send(msg);
  }

  /**
   * Request scene export from Blender
   * The scene will be sent via the 'scene-upload' event when ready.
   *
   * @param {Object} [options] - Export options
   * @param {boolean} [options.materialx=true] - Include MaterialX networks
   * @param {boolean} [options.animation=false] - Include animations
   * @param {boolean} [options.selectedOnly=false] - Export selected objects only
   * @returns {Promise<Object>} Resolves when scene is received
   */
  requestExport(options = {}) {
    return new Promise((resolve, reject) => {
      if (!this.connected || !this.ws) {
        reject(new Error('Not connected'));
        return;
      }

      const messageId = crypto.randomUUID();

      // Set up one-time listener for the scene
      const onScene = (event) => {
        this.removeEventListener('scene-upload', onScene);
        this.removeEventListener('server-error', onError);
        resolve(event.detail);
      };

      const onError = (event) => {
        this.removeEventListener('scene-upload', onScene);
        this.removeEventListener('server-error', onError);
        reject(new Error(event.detail.message || 'Export failed'));
      };

      this.addEventListener('scene-upload', onScene);
      this.addEventListener('server-error', onError);

      // Send export request
      const msg = encodeMessage({
        type: MessageType.EXPORT_REQUEST,
        messageId: messageId,
        exportOptions: {
          materialx: options.materialx !== false,
          animation: options.animation || false,
          selectedOnly: options.selectedOnly || false
        }
      });

      this.ws.send(msg);

      // Timeout after 60 seconds
      setTimeout(() => {
        this.removeEventListener('scene-upload', onScene);
        this.removeEventListener('server-error', onError);
        reject(new Error('Export timeout'));
      }, 60000);
    });
  }

  /**
   * Execute Python code in Blender
   *
   * @param {string} code - Python code to execute
   * @returns {Promise<Object>} Result with success, output, or error
   */
  executeCode(code) {
    return new Promise((resolve, reject) => {
      if (!this.connected || !this.ws) {
        reject(new Error('Not connected'));
        return;
      }

      const messageId = crypto.randomUUID();

      const onResult = (event) => {
        this.removeEventListener('code-result', onResult);
        resolve(event.detail);
      };

      this.addEventListener('code-result', onResult);

      const msg = encodeMessage({
        type: MessageType.EXECUTE_CODE,
        messageId: messageId,
        code: code
      });

      this.ws.send(msg);

      // Timeout after 30 seconds
      setTimeout(() => {
        this.removeEventListener('code-result', onResult);
        reject(new Error('Code execution timeout'));
      }, 30000);
    });
  }
}

export default BridgeClient;
