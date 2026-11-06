/* mermaid_init.js — theme-aware mermaid for the furo book.
 *
 * Furo stores its theme in localStorage ("theme": auto | light | dark)
 * and re-skins by swapping the <body> class; client-side rendered
 * diagrams (mermaid) must therefore be initialized with the matching
 * mermaid theme and re-rendered when the theme toggles. mermaid 10 is
 * loaded from a CDN by conf.py (html_js_files) before this file.
 *
 * Failure is silent: if mermaid never loads (offline), the diagrams
 * degrade to their readable source text, and the book still works.
 */
(function () {
  "use strict";

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
    try {
      window.mermaid.initialize({
        startOnLoad: false,
        theme: isDark() ? "dark" : "default",
        securityLevel: "strict",
      });
      /* Re-process every diagram (mermaid marks the ones it already
       * rendered; strip the mark so a theme toggle re-renders them). */
      document.querySelectorAll("pre.mermaid").forEach(function (el) {
        el.removeAttribute("data-processed");
      });
      if (typeof window.mermaid.run === "function") {
        window.mermaid.run();
      }
    } catch (e) { /* a bad diagram never takes the page down */ }
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", apply);
  } else {
    apply();
  }
  /* Furo dispatches this on a theme toggle (furo >= 2023.9.7). */
  document.addEventListener("furo:theme-change", apply);
})();
