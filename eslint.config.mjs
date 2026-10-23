import js from "@eslint/js";
import { defineConfig } from "eslint/config";
import globals from "globals";

// The Node scripts outside web/, which has its own config.
export default defineConfig([
  {
    files: ["tools/**/*.mjs", "firmware/test/**/*.mjs"],
    extends: [js.configs.recommended],
    languageOptions: {
      globals: globals.node,
    },
  },
]);
