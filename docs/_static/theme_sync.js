/* theme_sync.js — carry furo's Light/Dark/Auto choice across pages opened
 * straight from the file system.
 *
 * The why: furo persists the reader's pick in localStorage ("theme") and
 * re-reads it in its top-of-body reader script on every page load. Firefox
 * (privacy.file_unique_origin, the stock default) gives EVERY file://
 * document its own storage partition, so opening docs/_build/html/*.html
 * directly the toggle never survives a navigation: the next page reads
 * null, falls back to "auto" and follows the OS — "some pages auto-switch
 * to bright, the same the other way around". Headless automation profiles
 * flip that pref, which is why test drivers see shared storage and the
 * bug looks unreproducible there.
 *
 * The how: storage is not to be trusted on file://, so the state rides
 * the URL instead. The active mode travels as ?m=light|dark|auto on the
 * links the reader follows (rewritten on click), is seeded into
 * localStorage (best effort) BEFORE furo's reader runs — the head
 * placement comes from docs/_templates/page.html's extrahead override —
 * and is re-stamped
 * into the address bar (history.replaceState) when the toggle is clicked.
 * A bookmark taken after a toggle carries the mode too. Over http:// or
 * in Chrome the storage is shared and this merely re-affirms it.
 *
 * The click handlers are document-level delegation, so they never race
 * furo's own code: furo's listeners sit on the toggle buttons (target
 * phase), ours on the document (bubble) and therefore run AFTER the
 * cycle — the result is read from body[data-theme] without touching any
 * furo internals. "furo:theme-change" is ours as well: the vendored furo
 * never dispatches it, so mermaid_init.js (which re-skins the client-side
 * diagrams on that event) waited in vain — toggling left the diagrams in
 * the old theme.
 *
 * Failure is silent everywhere: a page with a denied storage API still
 * renders, and a bad click never eats the navigation.
 */
(function () {
  "use strict";

  var PARAM = "m";
  var MODES = { auto: 1, light: 1, dark: 1 };
  /* Links never stamped: external/other-protocol and pure in-page
   * fragments (same document, theme already applied). */
  var SKIP = /^(?:https?:|mailto:|javascript:|tel:|#)/i;
  /* Asset links (images, films, the lab widgets, downloads): a stray
   * query would only add noise or break caching expectations. */
  var ASSET = /\.(?:png|jpe?g|gif|svg|webp|ico|mp4|webm|pdf|zip|css|js|map|json|txt|bin)(?:[#?]|$)/i;

  function carriedMode() {
    try {
      var m = new URLSearchParams(window.location.search).get(PARAM);
      return m && MODES[m] ? m : null;
    } catch (e) {
      return null;
    }
  }

  var carried = carriedMode();
  if (carried) {
    /* Seed furo's own store BEFORE its body reader runs (this file is
     * loaded from <head> — see docs/_templates/page.html): no flash,
     * and the Light/Dark/Auto cycle starts from the truth. */
    try {
      window.localStorage.setItem("theme", carried);
    } catch (e) { /* partitioned or denied: the body fallback below */ }
  }

  document.addEventListener("DOMContentLoaded", function () {
    var body = document.body;
    if (carried && body.dataset.theme !== carried) {
      /* furo's reader could not run (storage access threw outright) —
       * apply the carried mode ourselves; a hair of flash beats a
       * wrong-theme page. */
      body.dataset.theme = carried;
    }
    /* Only carry a mode the reader actually chose (a URL param, or a
     * toggle on this page) — a fresh page must never stamp its "auto"
     * onto the site and pin the whole journey to the OS setting. */
    var explicit = !!carried;

    document.addEventListener("click", function (ev) {
      try {
        if (!ev.target || !ev.target.closest) {
          return;
        }
        if (ev.target.closest(".theme-toggle")) {
          /* furo cycled the mode already (its handler ran on the
           * button, target phase, before this bubble). Mirror the
           * result into the URL and re-skin the mermaid diagrams. */
          var next = body.dataset.theme || "auto";
          explicit = true;
          var here = new URL(window.location.href);
          here.searchParams.set(PARAM, next);
          window.history.replaceState(window.history.state, "", here);
          document.dispatchEvent(
            new CustomEvent("furo:theme-change", { detail: { theme: next } })
          );
          return;
        }
        if (!explicit) {
          return;
        }
        var a = ev.target.closest("a[href]");
        if (!a || a.hasAttribute("download")) {
          return;
        }
        var href = a.getAttribute("href");
        if (!href || SKIP.test(href) || ASSET.test(href)) {
          return;
        }
        var u = new URL(href, window.location.href);
        if (u.protocol !== window.location.protocol) {
          return;
        }
        u.searchParams.set(PARAM, body.dataset.theme || "auto");
        a.setAttribute("href", u.href);
      } catch (e) { /* the click stays the browser's */ }
    });
  });
})();
