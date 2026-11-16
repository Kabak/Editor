#pragma once
// Registers the extra encodings that Qt's QTextCodec provides (Chinese, Japanese,
// Korean, ...) with the table-driven encoder in encoding.h. It does nothing when
// the build has no QTextCodec (Qt 6 without the Core5Compat module). Safe to call repeatedly.
void registerQtCodecs();
