import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";
import { viteSingleFile } from "vite-plugin-singlefile";

// During `vite dev` the API calls go to the real device, so the page can be
// developed with hot reload without flashing. Override the target with
// DEVICE=http://10.47.0.1 when the device is on its own setup AP.
const device = process.env.DEVICE || "http://192.168.10.172";
const api = [
  "/status",
  "/login",
  "/logout",
  "/config",
  "/alerts",
  "/scan",
  "/save",
  "/ota",
  "/update",
  "/reboot",
  "/reset",
  "/i2c",
  "/touch",
  "/oled",
  "/gpio",
  "/fallback",
  "/screen",
  "/sensors",
  "/plugins",
];

// Two builds. The default one is a single file the firmware embeds. `--mode hosted`
// is the bundle published at https://cgdn1.swithy.com/v/<HOSTED_VERSION>/ (see
// tools/publish_web.mjs): ordinary files with hashed names, an absolute base so the
// chunks load from the site while the page itself lives on the device.
const hostedBase = "https://cgdn1.swithy.com/v/";

export default defineConfig(({ mode }) => {
  const server = {
    host: true,
    // The device refuses a request whose Origin is not its own page.
    proxy: Object.fromEntries(
      api.map((p) => [p, { target: device, changeOrigin: true, headers: { origin: device } }]),
    ),
  };
  if (mode === "hosted") {
    const version = process.env.HOSTED_VERSION;
    if (!version) {
      throw new Error("HOSTED_VERSION is required for --mode hosted");
    }
    return {
      base: `${hostedBase}${version}/`,
      plugins: [react(), tailwindcss()],
      // One script: the device hands the browser a hash per file it names, and a
      // chunk the entry imported on its own would load unchecked.
      build: {
        target: "es2020",
        outDir: "dist-hosted",
        manifest: true,
        cssCodeSplit: false,
        rollupOptions: { output: { inlineDynamicImports: true } },
      },
      server,
    };
  }
  return {
    plugins: [react(), tailwindcss(), viteSingleFile()],
    build: { target: "es2020", cssCodeSplit: false, assetsInlineLimit: 100000000 },
    server,
  };
});
