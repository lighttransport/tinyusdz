// Node-side JS texture pipeline: a pool of worker_threads each running
// texture-worker-node.mjs (pngjs decode -> gamma-aware box resize -> pngjs
// encode via node's native zlib). Plugs into convertFolderToUSDZ's
// `textureProcessor` hook; PNG work parallelizes across cores while non-PNG
// textures return null and fall back to the (sequential) WASM convertImage.
//
//   const { processor, destroy } = createNodeTextureProcessor({ concurrency: 8 });
//   await convertFolderToUSDZ(map, { textureProcessor: processor,
//                                    textureConcurrency: 8, ... });
//   destroy();
import os from 'node:os';
import { Worker } from 'node:worker_threads';

export function createNodeTextureProcessor(opts = {}) {
  const concurrency = Math.max(1, opts.concurrency || (os.cpus().length - 1));
  const workers = [];
  const idle = [];
  const waiters = [];
  let nextId = 1;

  for (let i = 0; i < concurrency; i++) {
    const w = new Worker(new URL('./texture-worker-node.mjs', import.meta.url));
    w.unref();
    w.inflight = new Map();
    w.on('message', (msg) => {
      const pending = w.inflight.get(msg.id);
      if (!pending) return;
      w.inflight.delete(msg.id);
      release(w);
      pending.resolve(msg);
    });
    w.on('error', (err) => {
      for (const pending of w.inflight.values()) pending.reject(err);
      w.inflight.clear();
      release(w);
    });
    workers.push(w);
    idle.push(w);
  }

  function release(w) {
    const waiter = waiters.shift();
    if (waiter) {
      waiter(w);
    } else {
      idle.push(w);
    }
  }

  function acquire() {
    const w = idle.pop();
    if (w) return Promise.resolve(w);
    return new Promise((resolve) => waiters.push(resolve));
  }

  // Matches the convertFolderToUSDZ textureProcessor hook contract. Returns
  // null (-> WASM fallback) for non-PNG inputs/outputs.
  async function processor({ data, maxTextureSize, reencode, textureFormat,
                             resizeColorspace }) {
    const fmt = textureFormat || 'keep';
    if (fmt !== 'png' && fmt !== 'keep') return null;

    const w = await acquire();
    const id = nextId++;
    // Copy into a transferable so the caller's view stays valid.
    const buf = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
    const reply = await new Promise((resolve, reject) => {
      w.inflight.set(id, { resolve, reject });
      w.postMessage({
        id,
        data: buf,
        maxTextureSize: maxTextureSize || 0,
        textureFormat: fmt,
        reencode: !!reencode,
        colorspace: resizeColorspace || 'linear',
      }, [buf]);
    });

    if (reply.skip) return null;
    if (reply.error) throw new Error(reply.error);
    return {
      data: new Uint8Array(reply.data),
      ext: 'png',
      resized: !!reply.resized,
      reencoded: true,
    };
  }

  function destroy() {
    for (const w of workers) w.terminate();
    workers.length = 0;
    idle.length = 0;
  }

  return { processor, destroy, concurrency };
}
