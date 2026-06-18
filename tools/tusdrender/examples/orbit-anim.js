// tusdrender -js animation example: orbit the camera around the scene and
// render a sequence of frames. The scene is loaded and the BVH is built ONCE
// (before this script runs); each tusdrender.render() reuses that resident BVH,
// so per-frame cost is just the ray trace -- this is memory-persistent
// rendering.
//
//   tusdrender -js tools/tusdrender/examples/orbit-anim.js scene.usdc
//
// (the positional output path is ignored in -js mode; the script chooses paths)

const b = tusdrender.bounds();
console.log("scene center=" + JSON.stringify(b.center) +
            " radius=" + b.radius.toFixed(1));

tusdrender.setResolution(640, 360);
tusdrender.setAmbient(0.25);
tusdrender.setBackground(1.0, 1.0, 1.0);
tusdrender.setShadows(true);

const frames = 24;
let total = 0.0;
for (let i = 0; i < frames; i++) {
  const azimuth = (360.0 * i) / frames;
  tusdrender.orbit(azimuth, 25.0, 2.4);  // azimuth, elevation, distance scale
  const name = "/tmp/anim_" + String(i).padStart(3, "0") + ".png";
  const r = tusdrender.render(name);
  total += r.seconds;
  console.log("frame " + i + "  azimuth=" + azimuth.toFixed(0) +
              "  trace=" + r.seconds.toFixed(3) + "s  -> " + r.path);
}

// The value of the last expression is returned to the host as JSON.
({ frames: frames, totalTraceSeconds: total,
   resolution: [tusdrender.stats().width, tusdrender.stats().height] });
