// Render an SVG to a PNG at its native size with headless Chromium.
//
//   node tools/brand/render_png.mjs <in.svg> <out.png> [scale]
//
// Used to produce docs/assets/brand/social-preview.png (upload it under the
// repository's Settings -> Social preview). Requires the `playwright` package
// and a Chromium build (PLAYWRIGHT_BROWSERS_PATH or executablePath).
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
let playwright;
try {
  playwright = require("playwright");
} catch {
  playwright = require(resolve(process.env.NODE_GLOBAL_MODULES ?? "/opt/node22/lib/node_modules", "playwright"));
}

const [input, output, scaleArg] = process.argv.slice(2);
if (!input || !output) {
  console.error("usage: render_png.mjs <in.svg> <out.png> [scale]");
  process.exit(64);
}
const svg = readFileSync(input, "utf8");
const m = svg.match(/<svg[^>]*\bwidth="(\d+)"[^>]*\bheight="(\d+)"/);
if (!m) throw new Error("SVG needs explicit width/height");
const [width, height] = [Number(m[1]), Number(m[2])];
const scale = Number(scaleArg ?? 1);

const browser = await playwright.chromium.launch();
const page = await browser.newPage({ viewport: { width, height }, deviceScaleFactor: scale });
await page.setContent(
  `<!doctype html><html><body style="margin:0;background:transparent">${svg}</body></html>`,
);
await page.locator("svg").first().screenshot({ path: output, omitBackground: true });
await browser.close();
console.log(`${output} ${width * scale}x${height * scale}`);
