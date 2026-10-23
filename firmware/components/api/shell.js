// Loads the hosted web interface named by the manifest the device verified (the
// JSON block in the page). Every file carries its sha384 hash, so the browser
// refuses a file the server changed; on any failure the page goes to /app, the
// interface built into the firmware.
(function () {
  // The theme before first paint, so there is no flash of the wrong one.
  try {
    var stored = localStorage.getItem("heroui-theme");
    var dark =
      !stored || stored === "system"
        ? window.matchMedia("(prefers-color-scheme: dark)").matches
        : stored === "dark";
    var el = document.documentElement;
    el.classList.add(dark ? "dark" : "light");
    el.setAttribute("data-theme", dark ? "dark" : "light");
    el.style.background = dark ? "#161618" : "#fafafa";
  } catch (e) {}

  var done = false;
  function fallback() {
    if (!done) {
      done = true;
      location.replace("/app" + location.hash);
    }
  }
  // A server that accepts the connection and never answers, or a build that throws
  // before it mounts, would leave a blank page: the app sets __cgdn1Ready once up.
  setTimeout(function () {
    if (!window.__cgdn1Ready) {
      fallback();
    }
  }, 10000);

  var m;
  try {
    m = JSON.parse(document.getElementById("cgdn1-web").textContent);
  } catch (e) {
    fallback();
    return;
  }
  for (var i = 0; i < m.css.length; i++) {
    var l = document.createElement("link");
    l.rel = "stylesheet";
    l.href = m.base + m.css[i];
    l.integrity = m.integrity[m.css[i]];
    l.crossOrigin = "anonymous";
    l.onerror = fallback;
    document.head.appendChild(l);
  }
  var s = document.createElement("script");
  s.type = "module";
  s.src = m.base + m.js;
  s.integrity = m.integrity[m.js];
  s.crossOrigin = "anonymous";
  s.onerror = fallback;
  document.head.appendChild(s);
})();
