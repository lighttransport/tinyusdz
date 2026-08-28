import { initDemo } from '../demo-foundation.js';
import { DEMO_BY_ID } from '../demo-configs.js';
import { installHairFurRendering } from '../hair-fur-rendering.js';

const app = await initDemo(DEMO_BY_ID['hair-fur']);
await installHairFurRendering(app);
