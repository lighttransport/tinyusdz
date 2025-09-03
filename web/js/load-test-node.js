// require nodejs v24.0 or later(to load wasm)
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';
import { TinyUSDZComposer } from 'tinyusdz/TinyUSDZComposer.js';

import fs from 'node:fs';

function reportMemUsage() {
 const used = process.memoryUsage()
 const messages = []
 for (let key in used) {
    console.log(`${key}: ${Math.round(used[key] / 1024 / 1024 * 100) / 100} MB`)
  }
}

function loadFile(filename) {
  try {
    const data = fs.readFileSync(filename);
    const mimeType = 'application/octet-stream';
    const blob = new Blob([data], { type: mimeType });

    const f = new File([blob], filename, { type: blob.type });

    return f;
    //const base64 = data.toString('base64');
    //data = null;
    //return `data:${mimeType};base64,${base64}`;
    //console.log(data);
  } catch (err) {
    console.error(err);
  }
  return null;
}

console.log(process.versions.node);

function checkMemory64Support() {
    try {
        // Try creating a 64-bit memory
        const memory = new WebAssembly.Memory({
            initial: 1,
            maximum: 65536,
            index: 'i64'  // This specifies 64-bit indexing
        });
        return true;
    } catch (e) {
        return false;
    }
}

console.log("memory64:", checkMemory64Support());

const usd_filename = "../../models/suzanne-subd-lv6.usdc";

async function initScene() {

  const loader = new TinyUSDZLoader();
  const memory64 = false; //checkMemory64Support();
  await loader.init({useMemory64: false});
  loader.setMaxMemoryLimitMB(200);

  const f = loadFile(usd_filename);

  const url = URL.createObjectURL(f);
  //const url = URL.createObjectURL(data);
  //const fdata = fetch(url);
  //const usd = await loader.loadTestAsync(url);
  //
  const usd = await loader.loadAsLayerAsync(url);

  //const usdRootNode = usd.getDefaultRootNode();
  //console.log(usdRootNode);

  reportMemUsage();

}

console.log(WebAssembly);
/*
Object [WebAssembly] {
  compile: [Function: compile],
  validate: [Function: validate],
  instantiate: [Function: instantiate]
}
*/


initScene();
