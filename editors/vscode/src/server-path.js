"use strict";

const path = require("node:path");

function executableName(platform = process.platform) {
  return platform === "win32" ? "janus-lsp.exe" : "janus-lsp";
}

function serverCandidates({
  configured,
  janusHome,
  home,
  pathCandidate,
  platform = process.platform,
}) {
  const executable = executableName(platform);
  return [
    configured && configured.trim(),
    janusHome && path.join(janusHome, "bin", executable),
    home && path.join(home, ".janus", "bin", executable),
    pathCandidate,
  ].filter(Boolean);
}

function selectServer(candidates, isExecutable) {
  return candidates.find(isExecutable);
}

module.exports = { executableName, serverCandidates, selectServer };
