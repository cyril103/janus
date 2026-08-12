const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const grammar = JSON.parse(
  fs.readFileSync(path.join(__dirname, "..", "syntaxes", "janus.tmLanguage.json"), "utf8"),
);
const numeric = grammar.patterns.find((pattern) => pattern.name === "constant.numeric.janus");
const regexp = new RegExp(`^(?:${numeric.match})$`);

test("TextMate recognizes every supported integer spelling", () => {
  for (const spelling of ["42", "1_000", "0xA2_0A", "0Xff", "0b1111_0000", "0B1010"])
    assert.match(spelling, regexp);
});

test("TextMate does not recognize malformed integer spellings", () => {
  for (const spelling of ["0x", "0b2", "0x_FF", "0xFF_", "0xF__F", "0_b1"])
    assert.doesNotMatch(spelling, regexp);
});

test("TextMate keeps decimal floating literals intact", () => {
  assert.equal("42.5".match(new RegExp(numeric.match))?.[0], "42.5");
});
