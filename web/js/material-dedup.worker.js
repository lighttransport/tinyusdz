import { TinyUSDZLoader } from './src/tinyusdz/TinyUSDZLoader.js';
import { isNextScene } from './src/tinyusdz/NextRenderSceneUtils.js';

let loader = null;
let activeBytes = null;
let activeName = '';

async function ensureLoader() {
	if (loader) return loader;
	loader = new TinyUSDZLoader(null, {
		suppressNativeInfoLogs: true
	});
	await loader.init();
	return loader;
}

function addTransferable(list, seen, value) {
	if (!value || !value.buffer) return;
	const buffer = value.buffer;
	if (!buffer || seen.has(buffer)) return;
	seen.add(buffer);
	list.push(buffer);
}

function serializeNextScene(usd, includeArchiveEntries = false) {
	if (!isNextScene(usd)) {
		throw new Error('worker conversion only supports next render scenes');
	}
	const archiveEntries = [];
	const transfer = [];
	const seen = new Set();
	if (includeArchiveEntries) {
		for (const [name, bytes] of usd.archiveEntries || []) {
			if (/\.(usd|usda|usdc)$/i.test(name)) continue;
			const copy = new Uint8Array(bytes);
			archiveEntries.push([name, copy]);
			addTransferable(transfer, seen, copy);
		}
	}
	for (const mesh of usd.meshes || []) {
		addTransferable(transfer, seen, mesh.points);
		addTransferable(transfer, seen, mesh.indices);
		addTransferable(transfer, seen, mesh.normals);
		addTransferable(transfer, seen, mesh.uv0);
	}
	return {
		payload: {
			__backend: 'next',
			filename: usd.filename || '',
			meshes: usd.meshes || [],
			stats: usd.getStats ? usd.getStats() : (usd.stats || {}),
			sceneMetadata: usd.getSceneMetadata ? usd.getSceneMetadata() : (usd.sceneMetadata || {}),
			archiveEntries
		},
		transfer
	};
}

async function convert(id, msg) {
	await ensureLoader();
	if (msg.bytes) {
		activeBytes = new Uint8Array(msg.bytes);
		activeName = msg.name || 'scene.usdz';
	}
	if (!activeBytes) {
		throw new Error('worker has no active USD bytes');
	}

	const { returnArchiveEntries = false, ...options } = msg.options || {};
	const progressBase = Number.isFinite(msg.progressBase) ? msg.progressBase : 0;
	const progressRange = Number.isFinite(msg.progressRange) ? msg.progressRange : 100;
	const report = (info = {}) => {
		self.postMessage({
			type: 'progress',
			id,
			info: {
				...info,
				progressBase,
				progressRange
			}
		});
	};

	const usd = await new Promise((resolve, reject) => {
		loader.parse(activeBytes, activeName, resolve, reject, {
			...options,
			backend: 'next',
			onProgress: report,
			onTinyUSDZDebug: report,
			onTydraProgress: report,
			progressBase,
			progressRange
		});
	});

	try {
		const { payload, transfer } = serializeNextScene(usd, !!returnArchiveEntries);
		self.postMessage({ type: 'result', id, payload }, transfer);
	} finally {
		if (usd && typeof usd.delete === 'function') {
			try { usd.delete(); } catch (_) {}
		}
	}
}

self.addEventListener('message', (event) => {
	const msg = event.data || {};
	if (msg.type === 'clear') {
		activeBytes = null;
		activeName = '';
		return;
	}
	if (msg.type !== 'convert') return;
	convert(msg.id, msg).catch((error) => {
		self.postMessage({
			type: 'error',
			id: msg.id,
			error: error?.message || String(error)
		});
	});
});
