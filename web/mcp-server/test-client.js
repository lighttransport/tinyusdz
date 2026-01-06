#!/usr/bin/env node

import { spawn } from 'child_process';
import { resolve } from 'path';

// Start the MCP server
const serverProcess = spawn('bun', ['run', 'server.ts'], {
  cwd: resolve('.'),
  stdio: ['pipe', 'pipe', 'pipe']
});

// MCP protocol messages
const initMessage = {
  jsonrpc: '2.0',
  id: 1,
  method: 'initialize',
  params: {
    protocolVersion: '2024-11-05',
    capabilities: {
      tools: {}
    },
    clientInfo: {
      name: 'test-client',
      version: '1.0.0'
    }
  }
};

const toolsListMessage = {
  jsonrpc: '2.0',
  id: 2,
  method: 'tools/list',
  params: {}
};

const addToolMessage = {
  jsonrpc: '2.0',
  id: 3,
  method: 'tools/call',
  params: {
    name: 'add',
    arguments: {
      a: 5,
      b: 3
    }
  }
};

let messageId = 0;

function sendMessage(message) {
  messageId++;
  const messageStr = JSON.stringify(message) + '\n';
  console.log(`Sending: ${messageStr.trim()}`);
  serverProcess.stdin.write(messageStr);
}

serverProcess.stdout.on('data', (data) => {
  const lines = data.toString().split('\n').filter(line => line.trim());
  lines.forEach(line => {
    if (line.startsWith('{')) {
      try {
        const response = JSON.parse(line);
        console.log('Received:', JSON.stringify(response, null, 2));
      } catch (e) {
        console.log('Raw response:', line);
      }
    } else {
      console.log('Server log:', line);
    }
  });
});

serverProcess.stderr.on('data', (data) => {
  console.error('Server error:', data.toString());
});

// Send messages in sequence
setTimeout(() => sendMessage(initMessage), 100);
setTimeout(() => sendMessage(toolsListMessage), 200);
setTimeout(() => sendMessage(addToolMessage), 300);

// Clean up after 5 seconds
setTimeout(() => {
  serverProcess.kill();
  process.exit(0);
}, 5000);