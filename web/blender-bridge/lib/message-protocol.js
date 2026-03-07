// Message Protocol for Blender Bridge
// Supports hybrid JSON header + binary payload format

import { v4 as uuidv4 } from 'uuid';

/**
 * Message Types
 */
export const MessageType = {
  // Blender -> Browser
  SCENE_UPLOAD: 'scene_upload',
  PARAMETER_UPDATE: 'parameter_update',

  // Browser -> Blender
  ACK: 'ack',
  STATUS: 'status',
  ERROR: 'error',
  EXPORT_REQUEST: 'export_request',      // Browser requests scene export
  EXECUTE_CODE: 'execute_code',          // Browser requests code execution

  // Blender -> Browser (responses)
  EXPORT_STARTED: 'export_started',
  EXPORT_PROGRESS: 'export_progress',
  CODE_RESULT: 'code_result',

  // Bidirectional
  PING: 'ping',
  PONG: 'pong'
};

/**
 * Parameter Target Types
 */
export const TargetType = {
  MATERIAL: 'material',
  LIGHT: 'light',
  CAMERA: 'camera',
  TRANSFORM: 'transform'
};

/**
 * Encode a message with optional binary payload
 * Format: [4 bytes header length (LE)] + [JSON header] + [binary payload]
 *
 * @param {Object} header - JSON header object
 * @param {Uint8Array|ArrayBuffer} [payload] - Optional binary payload
 * @returns {ArrayBuffer} Encoded message
 */
export function encodeMessage(header, payload = null) {
  // Ensure messageId and timestamp
  if (!header.messageId) {
    header.messageId = uuidv4();
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

  // Total size: 4 bytes (header length) + header + payload
  const totalSize = 4 + headerLength + payloadBytes.length;
  const result = new ArrayBuffer(totalSize);
  const view = new DataView(result);

  // Write header length (little-endian)
  view.setUint32(0, headerLength, true);

  // Write header
  new Uint8Array(result, 4, headerLength).set(headerBytes);

  // Write payload
  if (payloadBytes.length > 0) {
    new Uint8Array(result, 4 + headerLength).set(payloadBytes);
  }

  return result;
}

/**
 * Decode a message with optional binary payload
 *
 * @param {ArrayBuffer|Buffer} data - Raw message data
 * @returns {{ header: Object, payload: Uint8Array|null }} Decoded message
 */
export function decodeMessage(data) {
  // Convert Buffer to ArrayBuffer if needed (Node.js)
  let buffer;
  if (data instanceof ArrayBuffer) {
    buffer = data;
  } else if (Buffer.isBuffer(data)) {
    buffer = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
  } else {
    throw new Error('Invalid message data type');
  }

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
 * Create a scene upload message
 *
 * @param {Uint8Array|ArrayBuffer} usdzData - USDZ binary data
 * @param {Object} options - Scene options
 * @returns {ArrayBuffer} Encoded message
 */
export function createSceneUploadMessage(usdzData, options = {}) {
  const payload = usdzData instanceof Uint8Array ? usdzData : new Uint8Array(usdzData);

  const header = {
    type: MessageType.SCENE_UPLOAD,
    scene: {
      name: options.name || 'BlenderScene',
      format: 'usdz',
      byteLength: payload.length,
      hasAnimation: options.hasAnimation || false,
      exportOptions: {
        materialx: options.materialx !== false,
        rootPrimPath: options.rootPrimPath || '/World'
      }
    },
    metadata: {
      blenderVersion: options.blenderVersion || 'unknown',
      exportPlugin: 'native',
      upAxis: options.upAxis || 'Z'
    }
  };

  return encodeMessage(header, payload);
}

/**
 * Create a parameter update message
 *
 * @param {string} targetType - One of TargetType values
 * @param {string} path - USD prim path
 * @param {Object} changes - Changed parameters
 * @param {Object} [extra] - Extra target info (e.g., lightType, name)
 * @returns {ArrayBuffer} Encoded message
 */
export function createParameterUpdateMessage(targetType, path, changes, extra = {}) {
  const header = {
    type: MessageType.PARAMETER_UPDATE,
    target: {
      type: targetType,
      path: path,
      ...extra
    },
    changes: changes
  };

  return encodeMessage(header);
}

/**
 * Create an acknowledgment message
 *
 * @param {string} refMessageId - ID of the message being acknowledged
 * @param {string} status - 'success' or 'failed'
 * @param {Object} [extra] - Extra info (e.g., renderTime)
 * @returns {ArrayBuffer} Encoded message
 */
export function createAckMessage(refMessageId, status = 'success', extra = {}) {
  const header = {
    type: MessageType.ACK,
    refMessageId: refMessageId,
    status: status,
    ...extra
  };

  return encodeMessage(header);
}

/**
 * Create a status message
 *
 * @param {Object} viewerState - Viewer state info
 * @returns {ArrayBuffer} Encoded message
 */
export function createStatusMessage(viewerState) {
  const header = {
    type: MessageType.STATUS,
    viewer: viewerState
  };

  return encodeMessage(header);
}

/**
 * Create an error message
 *
 * @param {string} refMessageId - ID of the message that caused the error
 * @param {string} code - Error code
 * @param {string} message - Error message
 * @param {Object} [details] - Additional error details
 * @returns {ArrayBuffer} Encoded message
 */
export function createErrorMessage(refMessageId, code, message, details = {}) {
  const header = {
    type: MessageType.ERROR,
    refMessageId: refMessageId,
    error: {
      code: code,
      message: message,
      details: details
    }
  };

  return encodeMessage(header);
}

/**
 * Create ping/pong messages for heartbeat
 */
export function createPingMessage() {
  return encodeMessage({ type: MessageType.PING });
}

export function createPongMessage(refMessageId) {
  return encodeMessage({ type: MessageType.PONG, refMessageId });
}

/**
 * Create an export request message (Browser -> Blender)
 *
 * @param {Object} options - Export options
 * @returns {ArrayBuffer} Encoded message
 */
export function createExportRequestMessage(options = {}) {
  const header = {
    type: MessageType.EXPORT_REQUEST,
    exportOptions: {
      materialx: options.materialx !== false,
      animation: options.animation || false,
      selectedOnly: options.selectedOnly || false,
      rootPrimPath: options.rootPrimPath || '/World'
    }
  };

  return encodeMessage(header);
}

/**
 * Create a code execution request message (Browser -> Blender)
 *
 * @param {string} code - Python code to execute
 * @returns {ArrayBuffer} Encoded message
 */
export function createExecuteCodeMessage(code) {
  const header = {
    type: MessageType.EXECUTE_CODE,
    code: code
  };

  return encodeMessage(header);
}

// Export utility for browser compatibility
export const MessageProtocol = {
  MessageType,
  TargetType,
  encode: encodeMessage,
  decode: decodeMessage,
  createSceneUpload: createSceneUploadMessage,
  createParameterUpdate: createParameterUpdateMessage,
  createAck: createAckMessage,
  createStatus: createStatusMessage,
  createError: createErrorMessage,
  createPing: createPingMessage,
  createPong: createPongMessage
};

export default MessageProtocol;
