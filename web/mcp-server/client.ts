import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";

const transport = new StdioClientTransport({
  command: "bun",
  args: ["server.ts"]
});

const client = new Client(
  {
    name: "tinyusdz-example-mcp-client",
    version: "1.0.0"
  }
);

await client.connect(transport);

// List resources
const resources = await client.listResources();
console.log("resources", resources);

const tools = await client.listTools();
console.log("tools", tools);

const result = await client.callTool({
   name: "add",
   arguments: {
     a: 3,
     b: 5
   }
});
console.log(result);

/*
// Read a resource
const resource = await client.readResource({
  uri: "file:///example.txt"
});

// Call a tool
const result = await client.callTool({
  name: "example-tool",
  arguments: {
    arg1: "value"
  }
});
*/
