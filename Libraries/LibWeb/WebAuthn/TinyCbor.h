/*
 * Minimal CBOR writer for software-passkey attestation/COSE keys.
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/Error.h>
#include <AK/StringView.h>

namespace Web::WebAuthn::CBOR {

inline ErrorOr<void> append_u8(ByteBuffer& out, u8 value)
{
    TRY(out.try_append(value));
    return {};
}

inline ErrorOr<void> encode_uint(ByteBuffer& out, u64 value, u8 major)
{
    if (value < 24) {
        TRY(append_u8(out, major | static_cast<u8>(value)));
    } else if (value <= 0xff) {
        TRY(append_u8(out, major | 24));
        TRY(append_u8(out, static_cast<u8>(value)));
    } else if (value <= 0xffff) {
        TRY(append_u8(out, major | 25));
        TRY(append_u8(out, static_cast<u8>((value >> 8) & 0xff)));
        TRY(append_u8(out, static_cast<u8>(value & 0xff)));
    } else {
        return Error::from_string_literal("CBOR uint too large");
    }
    return {};
}

inline ErrorOr<void> encode_bytes(ByteBuffer& out, ReadonlyBytes bytes)
{
    TRY(encode_uint(out, bytes.size(), 0x40));
    TRY(out.try_append(bytes));
    return {};
}

inline ErrorOr<void> encode_text(ByteBuffer& out, StringView text)
{
    TRY(encode_uint(out, text.length(), 0x60));
    TRY(out.try_append(text.bytes()));
    return {};
}

inline ErrorOr<void> encode_map_start(ByteBuffer& out, size_t count)
{
    return encode_uint(out, count, 0xa0);
}

inline ErrorOr<void> encode_nint(ByteBuffer& out, i64 value)
{
    // CBOR negative integer: -1 - n
    VERIFY(value < 0);
    u64 n = static_cast<u64>(-1 - value);
    return encode_uint(out, n, 0x20);
}

inline ErrorOr<void> encode_int(ByteBuffer& out, i64 value)
{
    if (value >= 0)
        return encode_uint(out, static_cast<u64>(value), 0x00);
    return encode_nint(out, value);
}

}
