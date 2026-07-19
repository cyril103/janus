"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");
const childProcess = require("child_process");
const vscode = require("vscode");
const {
  LanguageClient,
  TransportKind,
} = require("vscode-languageclient/node");

let client;

function executableName() {
  return process.platform === "win32" ? "janus-lsp.exe" : "janus-lsp";
}

function isExecutable(candidate) {
  if (!candidate) {
    return false;
  }
  try {
    fs.accessSync(
      candidate,
      process.platform === "win32" ? fs.constants.F_OK : fs.constants.X_OK,
    );
    return true;
  } catch {
    return false;
  }
}

function findOnPath() {
  const command = process.platform === "win32" ? "where" : "which";
  const result = childProcess.spawnSync(command, [executableName()], {
    encoding: "utf8",
    windowsHide: true,
  });
  if (result.status !== 0) {
    return undefined;
  }
  return result.stdout.split(/\r?\n/).find(isExecutable);
}

function findServer() {
  const configured = vscode.workspace
    .getConfiguration("janus")
    .get("server.path", "")
    .trim();
  const home = process.env.JANUS_HOME;
  const candidates = [
    configured,
    home && path.join(home, "bin", executableName()),
    path.join(os.homedir(), ".janus", "bin", executableName()),
    findOnPath(),
  ];
  return candidates.find(isExecutable);
}

async function activate(context) {
  const server = findServer();
  if (!server) {
    const choice = await vscode.window.showErrorMessage(
      "janus-lsp est introuvable. Installe Janus ou configure janus.server.path.",
      "Ouvrir les paramètres",
    );
    if (choice === "Ouvrir les paramètres") {
      await vscode.commands.executeCommand(
        "workbench.action.openSettings",
        "janus.server.path",
      );
    }
    return;
  }

  client = new LanguageClient(
    "janus",
    "Janus Language Server",
    {
      command: server,
      transport: TransportKind.stdio,
    },
    {
      documentSelector: [{ scheme: "file", language: "janus" }],
      synchronize: {
        fileEvents:
          vscode.workspace.createFileSystemWatcher("**/.janusfmt"),
      },
    },
  );
  context.subscriptions.push(client);
  await client.start();
}

async function deactivate() {
  if (client) {
    await client.stop();
  }
}

module.exports = { activate, deactivate, findServer };
