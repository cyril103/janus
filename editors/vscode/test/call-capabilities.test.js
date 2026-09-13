const assert = require('node:assert/strict');
const test = require('node:test');
const grammar = require('../syntaxes/janus.tmLanguage.json');
test('call capabilities are contextual type qualifiers', () => {
  const rule = grammar.patterns.find(pattern => pattern.name === 'storage.type.function.janus');
  const match = new RegExp(rule.match);
  for (const capability of ['Fn', 'FnMut', 'FnOnce']) {
    assert(match.test(`${capability} (int) => int`));
    assert(!match.test(`val ${capability} = 42`));
    assert(!match.test(`My${capability} (int)`));
  }
});
