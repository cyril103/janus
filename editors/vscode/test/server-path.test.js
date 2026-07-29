"use strict";

const assert = require("node:assert/strict");
const test = require("node:test");
const path = require("node:path");

const {
  executableName,
  serverCandidates,
  selectServer,
} = require("../src/server-path");

test("configured janus-lsp wins over managed and PATH installations", () => {
  const configured = path.join("custom", "janus-lsp");
  const candidates = serverCandidates({
    configured,
    janusHome: path.join("managed", "janus"),
    home: path.join("user", "home"),
    pathCandidate: path.join("path", "janus-lsp"),
    platform: "linux",
  });

  assert.equal(candidates[0], configured);
  assert.equal(selectServer(candidates, candidate => candidate === configured),
               configured);
});

test("automatic selection supports install and update layouts", () => {
  const managed = serverCandidates({
    configured: "",
    janusHome: path.join("janusup", "stable"),
    home: path.join("user", "home"),
    pathCandidate: undefined,
    platform: "linux",
  });
  const updated = serverCandidates({
    configured: "",
    janusHome: path.join("janusup", "beta"),
    home: path.join("user", "home"),
    pathCandidate: undefined,
    platform: "linux",
  });

  assert.equal(selectServer(managed, () => true),
               path.join("janusup", "stable", "bin", "janus-lsp"));
  assert.equal(selectServer(updated, () => true),
               path.join("janusup", "beta", "bin", "janus-lsp"));
});

test("Windows uses janus-lsp.exe", () => {
  assert.equal(executableName("win32"), "janus-lsp.exe");
  assert.equal(executableName("linux"), "janus-lsp");
});
