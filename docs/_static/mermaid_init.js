/* mermaid_init.js — theme-aware mermaid for the furo book.
 *
 * Furo stores its theme in localStorage ("theme": auto | light | dark)
 * and re-skins via <body data-theme>; client-side rendered diagrams
 * (mermaid) must therefore be initialized with the matching mermaid
 * theme and re-rendered when the theme toggles. mermaid 10 is loaded
 * from a CDN by conf.py (html_js_files) before this file. theme_sync.js
 * seeds the localStorage entry from the ?m= URL carry, so the read
 * below agrees with the visible theme even on file:// (where Firefox
 * partitions storage per document).
 *
 * The re-render contract, learned the hard way: mermaid 10 replaces a
 * pre.mermaid's SOURCE with the rendered <svg> and stamps
 * data-processed. Stripping the mark alone makes the next run() parse
 * the SVG as diagram source (throws — and as an async rejection,
 * invisible to try/catch). So every first-render source is remembered
 * (a WeakMap keeps the DOM clean), each pass restores the source
 * before re-running, run()'s promise is caught, and a busy-flag
 * serializes passes (initialize() mutating the global config under an
 * in-flight render corrupts both).
 *
 * Failure is silent: if mermaid never loads (offline), the diagrams
 * degrade to their readable source text, and the book still works.
 */
(function () {
  "use strict";

  var sources = new WeakMap();
  var busy = false;
  var again = false;

  function isDark() {
    var t = null;
    try {
      t = localStorage.getItem("theme");
    } catch (e) { /* private browsing: fall through to media query */ }
    if (t === "dark") return true;
    if (t === "light") return false;
    return window.matchMedia("(prefers-color-scheme: dark)").matches;
  }

  function apply() {
    if (!window.mermaid || typeof window.mermaid.initialize !== "function") {
      return; /* mermaid.min.js not (yet) loaded; the source text stays */
    }
    if (busy) {
      again = true; /* re-render after the pass in flight used the new config */
      return;
    }
    busy = true;
    var done = function () {
      busy = false;
      if (again) {
        again = false;
        apply();
      }
    };
    try {
      window.mermaid.initialize({
        startOnLoad: false,
        theme: isDark() ? "dark" : "default",
        securityLevel: "strict",
      });
      document.querySelectorAll("pre.mermaid").forEach(function (el) {
        if (!sources.has(el)) {
          sources.set(el, el.textContent); /* before the first run rewrites it */
        }
        el.textContent = sources.get(el); /* drop any rendered <svg>, restore source */
        el.removeAttribute("data-processed");
      });
      if (typeof window.mermaid.run === "function") {
        var run = window.mermaid.run();
        if (run && typeof run.then === "function") {
          run.then(done, done); /* a bad diagram never takes the page down */
          return;
        }
      }
    } catch (e) { /* same — never fatal, and never left busy */ }
    done();
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", apply);
  } else {
    apply();
  }
  /* Re-render on a theme toggle. The vendored furo (2025.12.19) does NOT
   * dispatch this event itself — theme_sync.js does, after the reader
   * clicks the toggle (see its header for why the toggle needs a helper
   * at all on file://). */
  document.addEventListener("furo:theme-change", apply);
})();
