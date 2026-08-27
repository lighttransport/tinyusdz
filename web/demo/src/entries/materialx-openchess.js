import { initDemo } from '../demo-foundation.js';
import { DEMO_BY_ID } from '../demo-configs.js';
import { installOpenChessRendering } from '../openchess-rendering.js';

const app = await initDemo(DEMO_BY_ID['materialx-openchess']);
await installOpenChessRendering(app);
