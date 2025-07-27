import fs from "node:fs/promises";
import path from 'path';

import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StreamableHTTPClientTransport } from "@modelcontextprotocol/sdk/client/streamableHttp.js";
import { SSEClientTransport } from "@modelcontextprotocol/sdk/client/sse.js";

const assetFolder = "/mnt/n/data/tinyusdz/mcp/assets";
const url = "http://localhost:8085/mcp";

const descFilename = path.join(assetFolder, "asset-description.json")

const descriptions = JSON.parse(await fs.readFile(descFilename));
console.log(descriptions);

let client = null;
const baseUrl = new URL(url);
try {
  client = new Client({
    name: 'streamable-http-client',
    version: '1.0.0'
  });
  const transport = new StreamableHTTPClientTransport(
    new URL(baseUrl)
  );
  await client.connect(transport);
  console.log("Connected using Streamable HTTP transport");

} catch (error) {
  // If that fails with a 4xx error, try the older SSE transport
  console.log("Streamable HTTP connection failed, falling back to SSE transport");
  client = new Client({
    name: 'sse-client',
    version: '1.0.0'
  });
  const sseTransport = new SSEClientTransport(baseUrl);
  await client.connect(sseTransport);
  console.log("Connected using SSE transport");
}

const tools = await client.listTools();
console.log(tools);

for (const [filename, description] of Object.entries(descriptions)) {
  console.log(`filename: ${filename}, desc: ${description}`);

  const fullPath = path.join(assetFolder, filename);

  const base64data = await fs.readFile(fullPath, "base64");
  console.log(`base64data: ${base64data.substring(0, 100)}...`);

  await client.callTool({
    name: "load_usd_layer_from_data",
    arguments: {
      "name": filename,
      "data": base64data,
      "description": description
    }
  }).then((result) => {
    console.log("Setup asset result:", result);
  }).catch((error) => {
    console.error("Error setting up asset:", error);
  }); 

}


const descs = await client.callTool({
    name: "get_all_usd_descriptions",
    arguments: {
    }});
console.log("Descriptions:", descs);