import { initDemo } from '../demo-foundation.js';
import { DEMO_BY_ID } from '../demo-configs.js';
import { installMetaHumanRendering } from '../metahuman-rendering.js';

const app = await initDemo(DEMO_BY_ID.metahuman);
await installMetaHumanRendering(app);
