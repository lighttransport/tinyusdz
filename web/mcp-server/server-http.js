import express from "express";
import * as bodyParser from "body-parser";
//import { randomUUID } from "node:crypto";
import { v4 as uuidv4 } from "uuid";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { McpError, ErrorCode, ListResourceTemplatesRequestSchema, ReadResourceRequestSchema, ListToolsRequestSchema, CallToolRequestSchema, ListResourcesRequestSchema, ListPromptsRequestSchema, GetPromptRequestSchema, CompleteRequestSchema } from "@modelcontextprotocol/sdk/types.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
import { isInitializeRequest } from "@modelcontextprotocol/sdk/types.js";
import initTinyUSDZNative from "tinyusdz/tinyusdz.js";

import cors from 'cors';

//import { TinyUSDZMCPServer } from "tinyusdz/TinyUSDMCPServer.js";

const portno = 8085;

const app = express();
//app.use(express.json());

// Increase limit for larger requests(e.g. DataURI representation of USD file)
// Assume express 14.6.0+(bodyParser is now included in express.js)
app.use(express.json({limit: '50mb'})); // Increase limit for larger requests  
app.use(express.urlencoded({ extended: true, limit: '50mb' })); // Increase limit for larger requests    

// CORS configuration requied for browser-based MCP clients
app.use(cors({
  origin: '*', // Configure appropriately for production, for example:
  // origin: ['https://your-remote-domain.com', 'https://your-other-remote-domain.com'],
  exposedHeaders: ['Mcp-Session-Id'],
  allowedHeaders: ['Content-Type', 'mcp-session-id', 'mcp-protocol-version'],
}));

// Map to store transports by session ID
const transports = {};

const session = new Map();

initTinyUSDZNative().then(function (TinyUSDZ) {

  const tusd = new TinyUSDZ.TinyUSDZLoaderNative();
  // Handle POST requests for client-to-server communication
  app.post('/mcp', async (req, res) => {
    console.log("-- post --");
    console.log(req.headers);
    console.log(req.body);
    // Check for existing session ID
    const sessionId = req.headers['mcp-session-id'] || undefined;
    let transport = null;

    if (sessionId && transports[sessionId]) {
      // Reuse existing transport
      transport = transports[sessionId];
    } else if (!sessionId && isInitializeRequest(req.body)) {
      // New initialization request
      transport = new StreamableHTTPServerTransport({
        sessionIdGenerator: () => uuidv4(),
        onsessioninitialized: (sessionId) => {

          // Store the transport by session ID
          transports[sessionId] = transport;

          tusd.mcpCreateContext(sessionId);
        },
        // DNS rebinding protection is disabled by default for backwards compatibility. If you are running this server
        // locally, make sure to set:
        // enableDnsRebindingProtection: true,
        // allowedHosts: ['127.0.0.1'],
      });

      // Clean up transport when closed
      transport.onclose = () => {
        if (transport.sessionId) {
          delete transports[transport.sessionId];
        }
      };
      const server = new McpServer({
        name: "tinyusdz-mcp-server",
        version: "0.9.5"
      });

      server.server.registerCapabilities({
        resources: {
          listChanged: true
        },
        tools: {
          listChanged: true
        }
      });

      server.server.setRequestHandler(ListResourcesRequestSchema, async () => {
        const sessionId = server.server.transport.sessionId;
        console.log("list resources", sessionId);

        tusd.mcpSelectContext(sessionId);
        const resources_str = tusd.mcpResourcesList();
        console.log("resources_str", resources_str);

        const j = JSON.parse(resources_str);
        return j;
      });

      server.server.setRequestHandler(ReadResourceRequestSchema, async (request, extra) => {
        const sessionId = server.server.transport.sessionId;
        //console.log("read resource", sessionId);

        const uri = request.params.uri;
        //console.log("uri", uri);

        tusd.mcpSelectContext(sessionId);
        const resources_str = tusd.mcpResourcesRead(uri);
        //console.log("resources_str", resources_str);

        const j = JSON.parse(resources_str);
        return j;
      });

      server.server.setRequestHandler(ListToolsRequestSchema, async () => {
        const sessionId = server.server.transport.sessionId;

        console.log("sessId", sessionId);
        console.log("tusd", tusd);

        tusd.mcpSelectContext(sessionId);
        console.log("listtools");
        const tools_str = tusd.mcpToolsList();
        console.log("tools_str", tools_str);

        const j = JSON.parse(tools_str)
        return j;
      });

      server.server.setRequestHandler(CallToolRequestSchema, async (request, extra) => {
        console.log("request", request);

        const sessionId = server.server.transport.sessionId;

        //console.log("sessId", sessionId);
        //console.log("tusd", tusd);

        tusd.mcpSelectContext(sessionId);

        const tool_name = request.params.name;
        const args = JSON.stringify(request.params.arguments);
        console.log("tool_name", tool_name);
        //console.log("args", args);

        const result_str = tusd.mcpToolsCall(tool_name, args);
        //console.log("result_str", result_str);

        const j = JSON.parse(result_str)
        return j;
      });


      /*
      server.registerTool("get_version",
        {
          title: "Get TinyUSDZ version",
          description: "Get TinyUSDZ version",
          inputSchema: {}
        },
        async ({ }) => {

          return {
            content: [
              {
                type: 'text',
                text: "v0.9.0"
              }
            ],
          }
        });

      server.registerTool("load_usd_layer",
        {
          title: "Load USD as Layer from URI",
          description: "Add two numbers",
          inputSchema: { a: z.number(), b: z.number() }
        },
        async ({ a, b }) => ({
          content: [{ type: "text", text: String(a + b) }]
        })
      );
    */

      // ... set up server resources, tools, and prompts ...

      // Connect to the MCP server
      await server.connect(transport);
    } else {
      // Invalid request
      res.status(400).json({
        jsonrpc: '2.0',
        error: {
          code: -32000,
          message: 'Bad Request: No valid session ID provided',
        },
        id: null,
      });
      return;
    }

    // Handle the request
    await transport.handleRequest(req, res, req.body);
  });

  // Reusable handler for GET and DELETE requests
  const handleSessionRequest = async (req, res) => {
    const sessionId = req.headers['mcp-session-id'];
    if (!sessionId || !transports[sessionId]) {
      res.status(400).send('Invalid or missing session ID');
      return;
    }

    const transport = transports[sessionId];
    await transport.handleRequest(req, res);
  };

  // Handle GET requests for server-to-client notifications via SSE
  app.get('/mcp', handleSessionRequest);

  // Handle DELETE requests for session termination
  app.delete('/mcp', handleSessionRequest);

  console.log("localhost:" + portno.toString())
  app.listen(portno);

}).catch((error) => {
  console.error("Failed to initialize TinyUSDZLoader:", error);
});
