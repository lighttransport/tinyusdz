// Connection Manager for Blender Bridge WebSocket Server
// Handles session management, heartbeat, and client grouping

import { v4 as uuidv4 } from 'uuid';
import { createPingMessage, createPongMessage, decodeMessage, MessageType } from './message-protocol.js';

/**
 * Client types
 */
export const ClientType = {
  BLENDER: 'blender',
  BROWSER: 'browser'
};

/**
 * Connection info for a single client
 */
class ClientConnection {
  constructor(ws, sessionId, clientType) {
    this.ws = ws;
    this.sessionId = sessionId;
    this.clientType = clientType;
    this.connectedAt = Date.now();
    this.lastPing = Date.now();
    this.lastPong = Date.now();
    this.isAlive = true;
    this.metadata = {};
  }
}

/**
 * Session groups Blender source with browser viewers
 */
class Session {
  constructor(sessionId) {
    this.sessionId = sessionId;
    this.blenderClient = null;
    this.browserClients = new Map(); // clientId -> ClientConnection
    this.sceneState = null;
    this.createdAt = Date.now();
  }

  /**
   * Broadcast message to all browser clients in this session
   */
  broadcast(data, excludeClientId = null) {
    for (const [clientId, client] of this.browserClients) {
      if (clientId !== excludeClientId && client.ws.readyState === 1) { // WebSocket.OPEN
        client.ws.send(data);
      }
    }
  }

  /**
   * Get number of connected browsers
   */
  get browserCount() {
    return this.browserClients.size;
  }
}

/**
 * ConnectionManager handles all WebSocket connections
 */
export class ConnectionManager {
  constructor(options = {}) {
    this.sessions = new Map(); // sessionId -> Session
    this.clientToSession = new Map(); // clientId -> sessionId

    // Heartbeat configuration
    this.pingInterval = options.pingInterval || 30000; // 30 seconds
    this.pingTimeout = options.pingTimeout || 10000;   // 10 seconds timeout

    this._heartbeatTimer = null;
  }

  /**
   * Start heartbeat monitoring
   */
  startHeartbeat() {
    if (this._heartbeatTimer) return;

    this._heartbeatTimer = setInterval(() => {
      this._checkHeartbeats();
    }, this.pingInterval);
  }

  /**
   * Stop heartbeat monitoring
   */
  stopHeartbeat() {
    if (this._heartbeatTimer) {
      clearInterval(this._heartbeatTimer);
      this._heartbeatTimer = null;
    }
  }

  /**
   * Check all connections and send pings
   */
  _checkHeartbeats() {
    const now = Date.now();

    for (const session of this.sessions.values()) {
      // Check Blender client
      if (session.blenderClient) {
        this._pingClient(session.blenderClient, now);
      }

      // Check browser clients
      for (const client of session.browserClients.values()) {
        this._pingClient(client, now);
      }
    }
  }

  /**
   * Ping a single client
   */
  _pingClient(client, now) {
    if (!client.isAlive) {
      // Client didn't respond to last ping, terminate
      console.log(`Client ${client.sessionId} timed out, closing connection`);
      client.ws.terminate();
      return;
    }

    // Mark as not alive until pong received
    client.isAlive = false;
    client.lastPing = now;

    // Send ping
    if (client.ws.readyState === 1) { // WebSocket.OPEN
      try {
        const pingMsg = createPingMessage();
        client.ws.send(pingMsg);
      } catch (err) {
        console.error(`Failed to ping client: ${err.message}`);
      }
    }
  }

  /**
   * Handle pong response from client
   */
  handlePong(clientId) {
    const sessionId = this.clientToSession.get(clientId);
    if (!sessionId) return;

    const session = this.sessions.get(sessionId);
    if (!session) return;

    // Find client and mark as alive
    let client = null;
    if (session.blenderClient && session.blenderClient.sessionId === clientId) {
      client = session.blenderClient;
    } else {
      client = session.browserClients.get(clientId);
    }

    if (client) {
      client.isAlive = true;
      client.lastPong = Date.now();
    }
  }

  /**
   * Register a new Blender client (creates a new session)
   *
   * @param {WebSocket} ws - WebSocket connection
   * @param {Object} metadata - Connection metadata
   * @returns {string} Session ID
   */
  registerBlender(ws, metadata = {}) {
    const sessionId = uuidv4();
    const clientId = sessionId; // Blender client ID is same as session ID

    const session = new Session(sessionId);
    const client = new ClientConnection(ws, clientId, ClientType.BLENDER);
    client.metadata = metadata;

    session.blenderClient = client;
    this.sessions.set(sessionId, session);
    this.clientToSession.set(clientId, sessionId);

    console.log(`Blender client registered: session=${sessionId}`);
    return sessionId;
  }

  /**
   * Register a browser client to an existing session
   *
   * @param {WebSocket} ws - WebSocket connection
   * @param {string} sessionId - Session to join
   * @param {Object} metadata - Connection metadata
   * @returns {string|null} Client ID or null if session not found
   */
  registerBrowser(ws, sessionId, metadata = {}) {
    const session = this.sessions.get(sessionId);
    if (!session) {
      console.log(`Session not found: ${sessionId}`);
      return null;
    }

    const clientId = uuidv4();
    const client = new ClientConnection(ws, clientId, ClientType.BROWSER);
    client.metadata = metadata;

    session.browserClients.set(clientId, client);
    this.clientToSession.set(clientId, sessionId);

    console.log(`Browser client registered: session=${sessionId}, clientId=${clientId}`);
    return clientId;
  }

  /**
   * Unregister a client
   *
   * @param {string} clientId - Client to unregister
   */
  unregister(clientId) {
    const sessionId = this.clientToSession.get(clientId);
    if (!sessionId) return;

    const session = this.sessions.get(sessionId);
    if (!session) {
      this.clientToSession.delete(clientId);
      return;
    }

    // Check if it's the Blender client
    if (session.blenderClient && session.blenderClient.sessionId === clientId) {
      console.log(`Blender client disconnected: session=${sessionId}`);
      session.blenderClient = null;

      // If no Blender and no browsers, remove session
      if (session.browserClients.size === 0) {
        this.sessions.delete(sessionId);
        console.log(`Session removed: ${sessionId}`);
      }
    } else {
      // It's a browser client
      session.browserClients.delete(clientId);
      console.log(`Browser client disconnected: session=${sessionId}, clientId=${clientId}`);

      // If no Blender and no browsers, remove session
      if (!session.blenderClient && session.browserClients.size === 0) {
        this.sessions.delete(sessionId);
        console.log(`Session removed: ${sessionId}`);
      }
    }

    this.clientToSession.delete(clientId);
  }

  /**
   * Get session by ID
   *
   * @param {string} sessionId
   * @returns {Session|null}
   */
  getSession(sessionId) {
    return this.sessions.get(sessionId) || null;
  }

  /**
   * Get session ID for a client
   *
   * @param {string} clientId
   * @returns {string|null}
   */
  getSessionIdForClient(clientId) {
    return this.clientToSession.get(clientId) || null;
  }

  /**
   * Broadcast from Blender to all browsers in session
   *
   * @param {string} sessionId
   * @param {ArrayBuffer|Buffer} data
   */
  broadcastToBrowsers(sessionId, data) {
    const session = this.sessions.get(sessionId);
    if (!session) return;
    session.broadcast(data);
  }

  /**
   * Send to Blender client in session
   *
   * @param {string} sessionId
   * @param {ArrayBuffer|Buffer} data
   */
  sendToBlender(sessionId, data) {
    const session = this.sessions.get(sessionId);
    if (!session || !session.blenderClient) return;

    if (session.blenderClient.ws.readyState === 1) {
      session.blenderClient.ws.send(data);
    }
  }

  /**
   * Get all session IDs
   *
   * @returns {string[]}
   */
  getAllSessionIds() {
    return Array.from(this.sessions.keys());
  }

  /**
   * Get session statistics
   *
   * @returns {Object}
   */
  getStats() {
    const stats = {
      totalSessions: this.sessions.size,
      totalClients: this.clientToSession.size,
      sessions: []
    };

    for (const [sessionId, session] of this.sessions) {
      stats.sessions.push({
        sessionId,
        hasBlender: !!session.blenderClient,
        browserCount: session.browserClients.size,
        createdAt: session.createdAt
      });
    }

    return stats;
  }
}

export default ConnectionManager;
