import Stats from "stats.js";

const stats = new Stats();
stats.showPanel(0);

document.body.appendChild(stats.dom);

export default stats;
