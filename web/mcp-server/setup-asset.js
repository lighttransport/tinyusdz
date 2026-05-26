import fs from "node:fs/promises";
import path from 'path';

import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StreamableHTTPClientTransport } from "@modelcontextprotocol/sdk/client/streamableHttp.js";
import { SSEClientTransport } from "@modelcontextprotocol/sdk/client/sse.js";
import { assert } from "node:console";
import { isConstructorTypeNode } from "typescript";

// Function to get MIME type from file extension
function getMimeType(filename) {
  const ext = path.extname(filename).toLowerCase();
  const mimeTypes = {
    '.jpg': 'image/jpeg',
    '.jpeg': 'image/jpeg',
    '.png': 'image/png',
    '.gif': 'image/gif',
    '.webp': 'image/webp',
    '.bmp': 'image/bmp',
    '.svg': 'image/svg+xml'
  };
  return mimeTypes[ext] || 'application/octet-stream';
}

const assetFolder = "/mnt/n/data/tinyusdz/mcp/african_slate_quarry";
const url = "http://localhost:8085/mcp";

const descFilename = path.join(assetFolder, "asset-descriptions.json")

const descriptions = JSON.parse(await fs.readFile(descFilename));
console.log(descriptions);

let client = null;
const baseUrl = new URL(url);

// The HTTP MCP server requires a bearer token (see server-http.js). Read it
// from MCP_AUTH_TOKEN and attach it as an Authorization header to every
// request the transport makes.
const authToken = process.env.MCP_AUTH_TOKEN;
if (!authToken) {
  console.warn(
    "MCP_AUTH_TOKEN is not set; the server requires a bearer token and will " +
    "reject requests with 401. Set MCP_AUTH_TOKEN to the server's token."
  );
}
const requestInit = authToken
  ? { headers: { Authorization: `Bearer ${authToken}` } }
  : undefined;

try {
  client = new Client({
    name: 'streamable-http-client',
    version: '1.0.0'
  });
  const transport = new StreamableHTTPClientTransport(
    new URL(baseUrl),
    { requestInit }
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
  const sseTransport = new SSEClientTransport(baseUrl, {
    requestInit,
    eventSourceInit: requestInit ? { fetch: (u, init) => fetch(u, { ...init, headers: { ...init?.headers, ...requestInit.headers } }) } : undefined,
  });
  await client.connect(sseTransport);
  console.log("Connected using SSE transport");
}

const tools = await client.listTools();
//console.log(tools);

for (const [key, value] of Object.entries(descriptions)) {
  console.log(`Processing asset: ${key}`);
  const filename = value.usd_filename;
  const description = value.description;
  const preview = value.screenshot_filename;
  
  // Read geometry parameters from value, with defaults if not specified
  const pivot_position = value.pivot_position || [0.0, 0.0, 0.0];
  const bmin = value.bmin || [-1.0, -1.0, -1.0];
  const bmax = value.bmax || [1.0, 1.0, 1.0];
  
  assert(filename)
  assert(description)
  console.log(`filename: ${filename}, desc: ${description}`);
  console.log(`pivot_position: [${pivot_position.join(', ')}]`);
  console.log(`bmin: [${bmin.join(', ')}]`);
  console.log(`bmax: [${bmax.join(', ')}]`);
  if (!preview) {
    console.warn(`No preview image specified for ${filename}`);
  } else {
    console.log(`Preview image: ${preview}`);
  }

  const fullPath = path.join(assetFolder, filename);

  const base64data = await fs.readFile(fullPath, "base64");
  console.log(`base64data: ${base64data.substring(0, 100)}...`);

  let args = {
    "name": filename,
    "data": base64data,
    "description": description,
    "pivot_position": pivot_position,
    "bmin": bmin,
    "bmax": bmax
  };

  if (preview) {
    const previewPath = path.join(assetFolder, preview);
    const previewData = await fs.readFile(previewPath, "base64");
    console.log(`previewData: ${previewData.substring(0, 100)}...`);
    args.preview = {
      name: preview, // base filename
      data: previewData,
      mimeType: getMimeType(preview)
    };
  }

  console.log("args:", args);

  await client.callTool({
    name: "store_asset",
    arguments: args
  }).then((result) => {
    console.log("Setup asset result:", result);
  }).catch((error) => {
    console.error("Error setting up asset:", error);
  });

}


const descs = await client.callTool({
  name: "get_all_asset_descriptions",
  arguments: {
  }
});
console.log("Descriptions:", descs);
