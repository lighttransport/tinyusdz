// Test client for Blender Bridge
import { WebSocket } from 'ws';
import fs from 'fs';
import crypto from 'crypto';

const USDZ_PATH = '/tmp/blender_bridge_test.usdz';
const ws = new WebSocket('ws://localhost:8090?type=blender');
let sessionId = null;

ws.on('open', () => console.log('Blender client connected'));

ws.on('message', (data) => {
  const buffer = data instanceof Buffer ? data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength) : data;
  const view = new DataView(buffer);
  const headerLength = view.getUint32(0, true);
  const header = JSON.parse(new TextDecoder().decode(new Uint8Array(buffer, 4, headerLength)));

  if (header.type === 'session_created') {
    sessionId = header.sessionId;
    console.log('');
    console.log('========================================');
    console.log('SESSION ID:', sessionId);
    console.log('========================================');
    console.log('');
    console.log('Open browser: http://localhost:5173');
    console.log('Enter Session ID:', sessionId);
    console.log('');

    // Send scene
    const usdzData = fs.readFileSync(USDZ_PATH);
    const msgHeader = {
      type: 'scene_upload',
      messageId: crypto.randomUUID(),
      timestamp: Date.now(),
      scene: { name: 'BlenderScene', format: 'usdz', byteLength: usdzData.length },
      metadata: { blenderVersion: '5.0.1' }
    };
    const hdrBytes = Buffer.from(JSON.stringify(msgHeader));
    const result = Buffer.alloc(4 + hdrBytes.length + usdzData.length);
    result.writeUInt32LE(hdrBytes.length, 0);
    hdrBytes.copy(result, 4);
    usdzData.copy(result, 4 + hdrBytes.length);
    ws.send(result);
    console.log('Scene uploaded (' + usdzData.length + ' bytes)');
  } else if (header.type === 'ping') {
    const pong = JSON.stringify({
      type: 'pong',
      refMessageId: header.messageId,
      messageId: crypto.randomUUID(),
      timestamp: Date.now()
    });
    const pongBytes = Buffer.from(pong);
    const pongMsg = Buffer.alloc(4 + pongBytes.length);
    pongMsg.writeUInt32LE(pongBytes.length, 0);
    pongBytes.copy(pongMsg, 4);
    ws.send(pongMsg);
  } else if (header.type === 'ack') {
    console.log('ACK:', header.status);
  } else if (header.type === 'status') {
    console.log('Browser status:', JSON.stringify(header.viewer));
  }
});

ws.on('error', (err) => console.error('Error:', err.message));
ws.on('close', () => { console.log('Disconnected'); process.exit(0); });

// Keep alive for 10 minutes
setTimeout(() => { console.log('Timeout, closing...'); ws.close(); }, 600000);
console.log('Test client will stay connected for 10 minutes...');
