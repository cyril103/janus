# Text conversion and parsing

Import `std.text` for locale-independent primitive text conversion and parsing.

Janus `string` values are borrowed UTF-8 byte views with the ABI
`{ data, length }`. `length` is a byte count, not a Unicode character count,
and embedded zero bytes are part of the view. String literals have static
storage. A `TextBuilder` owns its heap allocation and frees it in its
destructor:

```janus
import std.text

val text : TextBuilder = new TextBuilder()
defer delete text
text.append("score=")
text.appendInt(score)
drawText(text.view(), 10, 10, 20, White)
```

`view()` borrows the builder's bytes. The view is valid only until the next
builder mutation (`append*` or `clear`) or until the builder is deleted. Do not
store it beyond that lifetime. Reusing one builder avoids an allocation for
each formatted frame. The builder also maintains a trailing NUL byte, so a
current view can safely cross existing C-string bridges such as `drawText`;
the NUL is not included in the view's byte length.

`appendInt`, `appendUInt`, `appendLong`, `appendULong`, `appendByte`,
`appendUByte`, `appendShort`, `appendUShort`, `appendISize`, `appendUSize`,
`appendFloat`, `appendDouble`, `appendBool`, and `appendChar` cover all
primitive numeric values and booleans. `append` concatenates a string view.
`appendHex` and `appendFixed` provide common typed formatting. Floating output
uses `.` and never depends on the process locale. Non-finite values are
rejected by the builder. `appendChar` emits valid UTF-8 and substitutes U+FFFD
for an invalid Unicode scalar.

The matching `parseInt`, `parseUInt`, `parseLong`, `parseULong`, `parseByte`,
`parseUByte`, `parseShort`, `parseUShort`, `parseISize`, `parseUSize`,
`parseFloat`, `parseDouble`, `parseBool`, and `parseChar` functions return
`Result[T, ParseError]`. `ParseError` distinguishes `Empty`, `Invalid`, `Sign`,
`Overflow`, `Underflow`, and `NonFinite`. Parsing consumes the entire byte
view, accepts no whitespace, rejects signs for unsigned values, and validates
UTF-8 scalar encoding for `char`.
