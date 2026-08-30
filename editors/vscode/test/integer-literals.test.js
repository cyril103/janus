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

test("TextMate recognizes every supported floating spelling as one scope", () => {
  for (const spelling of ["42.5", "1e10", "1E-10", "42.5e+3", "42.5f", "42.5e3f"])
    assert.equal(spelling.match(new RegExp(numeric.match))?.[0], spelling);
});

test("TextMate does not accept incomplete exponents as complete literals", () => {
  for (const spelling of ["1e", "1e+"])
    assert.doesNotMatch(spelling, regexp);
});

test("TextMate scopes bitwise operators separately from logical operators", () => {
  const bitwise = grammar.patterns.find(
    (pattern) => pattern.name === "keyword.operator.bitwise.janus",
  );
  const operator = new RegExp(`^(?:${bitwise.match})$`);
  for (const spelling of ["&", "|", "^", "<<", ">>"])
    assert.match(spelling, operator);
  for (const spelling of ["&&", "||", "<", ">"])
    assert.doesNotMatch(spelling, operator);
});

test("TextMate recognizes compound assignments as longest-match operators", () => {
  const operators = grammar.patterns.filter((pattern) =>
    pattern.name?.startsWith("keyword.operator"),
  );
  for (const spelling of ["+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="])
    assert.ok(operators.some((pattern) => new RegExp(`^(?:${pattern.match})$`).test(spelling)));
});

test("TextMate recognizes literal patterns and match guards", () => {
  const source = 'match opcode { uint(8) if opcode == uint(8) => "chip8", _ => "other" }';
  const keyword = grammar.patterns.find((pattern) => pattern.name === "keyword.control.janus");
  const keywords = [...source.matchAll(new RegExp(keyword.match, "g"))].map((match) => match[0]);
  assert.deepEqual(keywords, ["match", "if"]);
  assert.match("8", regexp);
  assert.match(source, /"chip8"/);
});

test("TextMate recognizes array literal delimiters and separators", () => {
  const punctuation = grammar.patterns.find(
    (pattern) => pattern.name === "punctuation.definition.array.janus",
  );
  const separator = grammar.patterns.find(
    (pattern) => pattern.name === "punctuation.separator.array.janus",
  );
  assert.match("[", new RegExp(`^(?:${punctuation.match})$`));
  assert.match("]", new RegExp(`^(?:${punctuation.match})$`));
  assert.match(",", new RegExp(`^(?:${separator.match})$`));
});

test("TextMate recognizes tailrec as a declaration modifier keyword", () => {
  const source = "private borrow tailrec def countDown(value : int) : int";
  const matches = grammar.patterns.flatMap((pattern) => {
    if (!pattern.match) return [];
    return [...source.matchAll(new RegExp(pattern.match, "g"))].map((match) => match[0]);
  });
  assert.ok(matches.includes("tailrec"));
});

test("TextMate recognizes only declaration-position extend", () => {
  const extension = grammar.patterns.find(
    (pattern) => pattern.captures?.["3"]?.name === "keyword.control.janus",
  );
  assert.ok(extension);
  assert.equal("private extend[T] Option[T] {".match(new RegExp(extension.match))?.[3], "extend");
  assert.doesNotMatch("values.extend(items)", new RegExp(extension.match));
  assert.doesNotMatch("def extend(value : int) : int", new RegExp(extension.match));
});

test("TextMate scopes function, lambda and match arrows consistently", () => {
  const arrow = grammar.patterns.find(
    (pattern) => pattern.name === "keyword.operator.arrow.janus",
  );
  assert.ok(arrow);
  const source = "def f(value : int) : int => match value { 0 => 1, _ => ((x : int) => x)(value) }";
  assert.equal([...source.matchAll(new RegExp(arrow.match, "g"))].length, 4);
});

test("TextMate recognizes contextual lambda parameter forms", () => {
  const source = [
    "value => value",
    "(left, right) => left",
    "() => 1",
    "(borrow value) => value",
    "(borrow var value) => value",
  ].join("\n");
  const arrow = grammar.patterns.find(
    (pattern) => pattern.name === "keyword.operator.arrow.janus",
  );
  assert.equal([...source.matchAll(new RegExp(arrow.match, "g"))].length, 5);
  const nonLambdaSource =
    "def f(value : int, other : int) : int => match value { 0 => other, _ => value }";
  assert.equal(
    [...nonLambdaSource.matchAll(new RegExp(arrow.match, "g"))].length,
    3,
  );
});
