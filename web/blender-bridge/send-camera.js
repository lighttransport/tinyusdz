// Send camera info to connected browsers
import { WebSocket } from 'ws';
import crypto from 'crypto';

const sessionId = process.argv[2];
const cameraJson = process.argv[3];

if (!sessionId || !cameraJson) {
  console.log('Usage: node send-camera.js <session-id> <camera-json>');
  process.exit(1);
}

const camera = JSON.parse(cameraJson);

const ws = new WebSocket(`ws://localhost:8090?type=blender&session=${sessionId}`);

ws.on('open', () => {
  // Send camera parameter update
  const msgHeader = {
    type: 'parameter_update',
    messageId: crypto.randomUUID(),
    timestamp: Date.now(),
    target: {
      type: 'camera',
      path: '/BlenderViewport'
    },
    changes: {
      position: camera.position,
      target: camera.target,
      fov: camera.lens ? (2 * Math.atan(18 / camera.lens) * 180 / Math.PI) : 45
    }
  };

  const headerJson = JSON.stringify(msgHeader);
  const headerBytes = Buffer.from(headerJson);
  const msg = Buffer.alloc(4 + headerBytes.length);
  msg.writeUInt32LE(headerBytes.length, 0);
  headerBytes.copy(msg, 4);

  ws.send(msg);
  console.log('Camera update sent:', msgHeader.changes);

  setTimeout(() => ws.close(), 500);
});

ws.on('error', (err) => console.error('Error:', err.message));
ws.on('close', () => process.exit(0));
