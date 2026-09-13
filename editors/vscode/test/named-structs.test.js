const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const grammar = JSON.parse(fs.readFileSync(path.join(__dirname, "../syntaxes/janus.tmLanguage.json"), "utf8"));

test("TextMate recognizes named construction types and fields", () => {
  const rule = grammar.patterns.find(p => p.name === "meta.construction.named.janus");
  const begin = new RegExp(rule.begin, "u");
  for (const source of ["new Point { x, y }", "new api.Pair[int, bool] { second: true, first: 1 }"])
    assert.ok(begin.test(source));
  assert.ok(!begin.test("new Point(1, 2)"));
  assert.ok(!begin.test("match point {"));
  const fields = new RegExp(rule.patterns[0].match, "gu");
  assert.deepEqual([..."{ x: horizontal, y, café: value }".matchAll(fields)].map(m => m[1]), ["x", "y", "café"]);
  assert.ok(rule.patterns.some(p => p.include === "#expression-braces"));
  assert.ok(grammar.repository["expression-braces"].patterns.some(p => p.include === "#expression-braces"));
  const storage = grammar.patterns.find(p => p.name === "storage.type.janus");
  assert.ok(new RegExp(storage.match).test("struct"));
});
