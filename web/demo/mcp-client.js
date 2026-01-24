import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StreamableHTTPClientTransport } from "@modelcontextprotocol/sdk/client/streamableHttp.js";
import { SSEClientTransport } from "@modelcontextprotocol/sdk/client/sse.js";

import { createCanvas } from "canvas";


//await Bun.write("bun.png", canvas.toBuffer());


const url = "http://localhost:8085/mcp"

function genImage() {
  const canvas = createCanvas(50, 50);
  const ctx = canvas.getContext('2d');

  ctx.fillStyle = "black";
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = "white";
  ctx.fillText("bora", 0, 30);

  const dataurl = canvas.toDataURL('image/jpeg', /* quality */0.8 );
  // strip mime prefix
  return dataurl.replace(/^.*,/, '');
}

console.log(genImage());

async function sendScreenshot(client) {

    const dataURI = genImage();

    await client.callTool({
      name: "save_screenshot",
      arguments: {
        "uri" : dataURI
      }
    });
}

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

//sendScreenshot(client)


