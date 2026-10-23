import React from "react";
import ReactDOM from "react-dom/client";
import { Router } from "wouter";
import { useHashLocation } from "wouter/use-hash-location";

import App from "./app/App";
import { LangProvider } from "./i18n/LangProvider";
import "./index.css";

// The device's shell page waits for this before it gives up on the hosted build.
window.__cgdn1Ready = true;

// Hash routing: the firmware serves the page as one file at "/".
ReactDOM.createRoot(document.getElementById("root")).render(
  <React.StrictMode>
    <LangProvider>
      <Router hook={useHashLocation}>
        <div className="min-h-screen bg-background text-foreground">
          <App />
        </div>
      </Router>
    </LangProvider>
  </React.StrictMode>,
);
