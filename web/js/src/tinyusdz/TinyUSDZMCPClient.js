import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StreamableHTTPClientTransport } from "@modelcontextprotocol/sdk/client/streamableHttp.js";
import { SSEClientTransport } from "@modelcontextprotocol/sdk/client/sse.js";


//let client = Client || undefined;
//const url = "http://localhost:8085/mcp";


class TinyUSDZMCPClient {

  constructor() {
    this.url = null;
    this.client = null;
    this.transport = null;
  }
  
  async connect(url) {
    this.url = url;
    try {
      this.client = new Client({
        name: 'tinyusdz-mcp-client',
        version: '0.9.0'
      });

      this.transport = new StreamableHTTPClientTransport(
        new URL(this.url)
      );
      await this.client.connect(this.transport);
      console.log("Connected using Streamable HTTP transport");
    } catch (error) {
      //// If that fails with a 4xx error, try the older SSE transport
      //console.log("Streamable HTTP connection failed, falling back to SSE transport");
      //client = new Client({
      //  name: 'sse-client',
      //  version: '1.0.0'
      //});
      //const sseTransport = new SSEClientTransport(baseUrl);
      //await client.connect(sseTransport);
      //console.log("Connected using SSE transport");
      //

      console.error(error);
    }

  }

}

// Test

const cli = new TinyUSDZMCPClient();
cli.connect("http://localhost:8085/mcp");
