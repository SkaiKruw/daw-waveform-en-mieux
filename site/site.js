// Onde : renseigne les boutons avec les fichiers de la dernière version publiée sur GitHub.
// Sans réseau ou sans version publiée, les boutons mènent à la page des versions (rien ne casse).
(function () {
  "use strict";
  var REPO = "SkaiKruw/onde";
  var ALLOWED = "https://github.com/" + REPO + "/releases/download/";   // seuls liens de téléchargement acceptés

  function $(id) { return document.getElementById(id); }
  function set(id, text) { var e = $(id); if (e) e.textContent = text; }
  function mb(bytes) { return Math.max(1, Math.round(bytes / 1048576)) + " Mo"; }
  function safe(url) { return typeof url === "string" && url.indexOf(ALLOWED) === 0 ? url : null; }

  var mac = $("mac-btn");
  mac.setAttribute("aria-disabled", "true");
  mac.addEventListener("click", function (e) {
    if (mac.getAttribute("aria-disabled") === "true") { e.preventDefault(); var m = $("mac"); if (m) m.scrollIntoView(); }
  });

  if (!window.fetch) return;
  fetch("https://api.github.com/repos/" + REPO + "/releases/latest", { headers: { Accept: "application/vnd.github+json" }, referrerPolicy: "no-referrer" })
    .then(function (r) { if (!r.ok) throw new Error(String(r.status)); return r.json(); })
    .then(function (rel) {
      var assets = Array.isArray(rel.assets) ? rel.assets : [];
      function pick(re) { for (var i = 0; i < assets.length; i++) if (re.test(String(assets[i].name)) && safe(assets[i].browser_download_url)) return assets[i]; return null; }
      var exe = pick(/setup.*\.exe$/i) || pick(/\.exe$/i);
      var zip = pick(/portable.*\.zip$/i);
      var dmg = pick(/\.dmg$/i);

      var tag = String(rel.tag_name || "").replace(/^v/i, "").slice(0, 20);
      if (tag) set("rel-version", tag);
      if (rel.published_at) {
        var d = new Date(rel.published_at);
        if (!isNaN(d)) { set("rel-date", d.toLocaleDateString("fr-FR", { day: "numeric", month: "short", year: "numeric" })); $("rel-date").hidden = false; $("rel-date-key").hidden = false; }
      }
      if (exe) {
        $("win-btn").href = exe.browser_download_url;
        set("rel-size", mb(exe.size));
        if (typeof exe.digest === "string" && /^sha256:[0-9a-f]{64}$/.test(exe.digest))
          set("checksum", "Empreinte SHA-256 de " + String(exe.name).slice(0, 60) + " : " + exe.digest.slice(7));
      }
      if (zip) $("win-zip").href = zip.browser_download_url;
      if (dmg) {
        mac.href = dmg.browser_download_url;
        mac.removeAttribute("aria-disabled");
        set("mac-label", "Télécharger pour macOS");
        set("mac-text", "macOS 11 ou plus récent, processeurs Apple et Intel. L'application n'est pas encore notariée : au premier lancement, clic droit sur Onde, puis Ouvrir.");
      }
    })
    .catch(function () { /* hors ligne ou aucune version : liens par défaut */ });
})();
