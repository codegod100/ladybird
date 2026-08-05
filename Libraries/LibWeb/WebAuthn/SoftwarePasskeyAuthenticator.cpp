/*
 * Experimental in-process software passkey authenticator.
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Base64.h>
#include <AK/Random.h>
#include <AK/StringBuilder.h>
#include <LibCrypto/BigInt/UnsignedBigInteger.h>
#include <LibCrypto/Curves/SECPxxxr1.h>
#include <LibCrypto/Hash/SHA2.h>
#include <LibJS/Runtime/ArrayBuffer.h>
#include <LibJS/Runtime/ValueInlines.h>
#include <LibURL/Origin.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/WebAuthn/AuthenticatorAssertionResponse.h>
#include <LibWeb/WebAuthn/AuthenticatorAttestationResponse.h>
#include <LibWeb/WebAuthn/PublicKeyCredential.h>
#include <LibWeb/WebAuthn/SoftwarePasskeyAuthenticator.h>
#include <LibWeb/WebAuthn/TinyCbor.h>
#include <LibWeb/WebIDL/AbstractOperations.h>
#include <LibWeb/WebIDL/DOMException.h>

namespace Web::WebAuthn {

struct StoredPasskey {
    ByteBuffer credential_id;
    ByteBuffer user_handle;
    ByteString rp_id;
    ByteBuffer private_key_bytes;
    u32 sign_count { 0 };
};

static Vector<StoredPasskey>& passkey_store()
{
    static Vector<StoredPasskey> store;
    return store;
}

static ByteString to_byte_string(String const& string)
{
    return ByteString { string.bytes() };
}

static WebIDL::ExceptionOr<ByteBuffer> lift(JS::Realm& realm, ErrorOr<ByteBuffer> result)
{
    if (result.is_error())
        return WebIDL::UnknownError::create(realm, "Software passkey internal error"_utf16);
    return result.release_value();
}

static WebIDL::ExceptionOr<String> lift_string(JS::Realm& realm, ErrorOr<String> result)
{
    if (result.is_error())
        return WebIDL::UnknownError::create(realm, "Software passkey internal error"_utf16);
    return result.release_value();
}

static WebIDL::ExceptionOr<ByteBuffer> sha256(JS::Realm& realm, ReadonlyBytes data)
{
    auto hasher = ::Crypto::Hash::SHA256::create();
    hasher->update(data);
    auto digest = hasher->digest();
    return lift(realm, ByteBuffer::copy(digest.bytes()));
}

static WebIDL::ExceptionOr<ByteBuffer> buffer_from_js(JS::Realm& realm, JS::Value value)
{
    if (!value.is_object() || !WebIDL::is_buffer_source_type(value))
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "Expected BufferSource"_utf16 };
    auto copy = WebIDL::get_buffer_source_copy(value.as_object());
    if (copy.is_error())
        return WebIDL::UnknownError::create(realm, "Failed to read BufferSource"_utf16);
    return copy.release_value();
}

static WebIDL::ExceptionOr<void> cbor_try(JS::Realm& realm, ErrorOr<void> result)
{
    if (result.is_error())
        return WebIDL::UnknownError::create(realm, "CBOR encode failed"_utf16);
    return {};
}

static WebIDL::ExceptionOr<ByteBuffer> encode_cose_ec2_es256(JS::Realm& realm, ReadonlyBytes x, ReadonlyBytes y)
{
    ByteBuffer out;
    TRY(cbor_try(realm, CBOR::encode_map_start(out, 5)));
    TRY(cbor_try(realm, CBOR::encode_int(out, 1)));
    TRY(cbor_try(realm, CBOR::encode_int(out, 2)));
    TRY(cbor_try(realm, CBOR::encode_int(out, 3)));
    TRY(cbor_try(realm, CBOR::encode_int(out, -7)));
    TRY(cbor_try(realm, CBOR::encode_int(out, -1)));
    TRY(cbor_try(realm, CBOR::encode_int(out, 1)));
    TRY(cbor_try(realm, CBOR::encode_int(out, -2)));
    TRY(cbor_try(realm, CBOR::encode_bytes(out, x)));
    TRY(cbor_try(realm, CBOR::encode_int(out, -3)));
    TRY(cbor_try(realm, CBOR::encode_bytes(out, y)));
    return out;
}

static WebIDL::ExceptionOr<ByteBuffer> encode_none_attestation_object(JS::Realm& realm, ReadonlyBytes auth_data)
{
    ByteBuffer out;
    TRY(cbor_try(realm, CBOR::encode_map_start(out, 3)));
    TRY(cbor_try(realm, CBOR::encode_text(out, "fmt"sv)));
    TRY(cbor_try(realm, CBOR::encode_text(out, "none"sv)));
    TRY(cbor_try(realm, CBOR::encode_text(out, "attStmt"sv)));
    TRY(cbor_try(realm, CBOR::encode_map_start(out, 0)));
    TRY(cbor_try(realm, CBOR::encode_text(out, "authData"sv)));
    TRY(cbor_try(realm, CBOR::encode_bytes(out, auth_data)));
    return out;
}

static WebIDL::ExceptionOr<ByteBuffer> build_client_data_json(JS::Realm& realm, StringView type, ReadonlyBytes challenge, URL::Origin const& origin)
{
    auto challenge_b64 = TRY(lift_string(realm, encode_base64url(challenge, AK::OmitPadding::Yes)));
    StringBuilder builder;
    builder.append("{\"type\":\""sv);
    builder.append(type);
    builder.append("\",\"challenge\":\""sv);
    builder.append(challenge_b64);
    builder.append("\",\"origin\":\""sv);
    builder.append(origin.serialize());
    builder.append("\",\"crossOrigin\":false}"sv);
    return lift(realm, builder.to_byte_buffer());
}

static WebIDL::ExceptionOr<ByteBuffer> make_authenticator_data(JS::Realm& realm, StringView rp_id, ReadonlyBytes credential_id, ReadonlyBytes cose_key, bool include_attested, u32 sign_count)
{
    auto rp_hash = TRY(sha256(realm, rp_id.bytes()));
    ByteBuffer auth_data;
    if (auth_data.try_append(rp_hash).is_error())
        return WebIDL::UnknownError::create(realm, "OOM"_utf16);

    // Match openbao-passkeys: UP | UV | BE | BS (Pocket ID requires BE/BS).
    u8 flags = 0x01 | 0x04 | 0x08 | 0x10; // UP | UV | BE | BS
    if (include_attested)
        flags |= 0x40; // AT
    if (auth_data.try_append(flags).is_error())
        return WebIDL::UnknownError::create(realm, "OOM"_utf16);

    u8 count_be[4] {
        static_cast<u8>((sign_count >> 24) & 0xff),
        static_cast<u8>((sign_count >> 16) & 0xff),
        static_cast<u8>((sign_count >> 8) & 0xff),
        static_cast<u8>(sign_count & 0xff),
    };
    if (auth_data.try_append(ReadonlyBytes { count_be, 4 }).is_error())
        return WebIDL::UnknownError::create(realm, "OOM"_utf16);

    if (include_attested) {
        u8 aaguid[16] {
            'L', 'a', 'd', 'y', 'b', 'i', 'r', 'd',
            0, 0, 0, 0, 0, 0, 0, 0
        };
        if (auth_data.try_append(ReadonlyBytes { aaguid, 16 }).is_error())
            return WebIDL::UnknownError::create(realm, "OOM"_utf16);
        u8 id_len[2] {
            static_cast<u8>((credential_id.size() >> 8) & 0xff),
            static_cast<u8>(credential_id.size() & 0xff),
        };
        if (auth_data.try_append(ReadonlyBytes { id_len, 2 }).is_error()
            || auth_data.try_append(credential_id).is_error()
            || auth_data.try_append(cose_key).is_error()) {
            return WebIDL::UnknownError::create(realm, "OOM"_utf16);
        }
    }
    return auth_data;
}

static WebIDL::ExceptionOr<ByteString> resolve_rp_id(JS::Realm& realm, JS::Object const& public_key_options, URL::Origin const& origin)
{
    auto& vm = realm.vm();
    auto rp_value = TRY(public_key_options.get("rp"_utf16_fly_string));
    if (rp_value.is_object()) {
        auto id_value = TRY(rp_value.as_object().get("id"_utf16_fly_string));
        if (!id_value.is_undefined()) {
            auto string = TRY(WebIDL::to_byte_string(vm, id_value));
            return to_byte_string(string);
        }
    }
    if (origin.is_opaque())
        return WebIDL::NotAllowedError::create(realm, "Passkey RP ID missing"_utf16);
    return to_byte_string(origin.host().serialize());
}

WebIDL::ExceptionOr<GC::Ref<PublicKeyCredential>> software_create_credential(JS::Realm& realm, JS::Object const& public_key_options)
{
    auto origin = HTML::current_settings_object().origin();

    auto challenge = TRY(buffer_from_js(realm, TRY(public_key_options.get("challenge"_utf16_fly_string))));
    auto rp_id = TRY(resolve_rp_id(realm, public_key_options, origin));

    auto user_value = TRY(public_key_options.get("user"_utf16_fly_string));
    if (!user_value.is_object())
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "publicKey.user required"_utf16 };
    auto user_handle = TRY(buffer_from_js(realm, TRY(user_value.as_object().get("id"_utf16_fly_string))));

    ::Crypto::Curves::SECP256r1 curve;
    auto private_key_or_error = curve.generate_private_key();
    if (private_key_or_error.is_error())
        return WebIDL::UnknownError::create(realm, "Keygen failed"_utf16);
    auto private_key = private_key_or_error.release_value();
    auto public_point_or_error = curve.generate_public_key(private_key);
    if (public_point_or_error.is_error())
        return WebIDL::UnknownError::create(realm, "Keygen failed"_utf16);
    auto public_point = public_point_or_error.release_value();
    auto x_or_error = public_point.x_bytes();
    auto y_or_error = public_point.y_bytes();
    if (x_or_error.is_error() || y_or_error.is_error())
        return WebIDL::UnknownError::create(realm, "Keygen failed"_utf16);
    auto cose_key = TRY(encode_cose_ec2_es256(realm, x_or_error.value(), y_or_error.value()));

    auto credential_id = TRY(lift(realm, ByteBuffer::create_uninitialized(16)));
    fill_with_random(credential_id);

    auto private_key_bytes_or_error = ::Crypto::Curves::SECPxxxr1Point::scalar_to_bytes(private_key, 32);
    if (private_key_bytes_or_error.is_error())
        return WebIDL::UnknownError::create(realm, "Keygen failed"_utf16);

    passkey_store().append(StoredPasskey {
        .credential_id = MUST(ByteBuffer::copy(credential_id)),
        .user_handle = MUST(ByteBuffer::copy(user_handle)),
        .rp_id = rp_id,
        .private_key_bytes = private_key_bytes_or_error.release_value(),
        .sign_count = 1,
    });

    auto client_data = TRY(build_client_data_json(realm, "webauthn.create"sv, challenge, origin));
    auto auth_data = TRY(make_authenticator_data(realm, rp_id, credential_id, cose_key, true, 1));
    auto attestation_object = TRY(encode_none_attestation_object(realm, auth_data));

    auto id_b64 = TRY(lift_string(realm, encode_base64url(credential_id, AK::OmitPadding::Yes)));
    auto client_data_ab = JS::ArrayBuffer::create(realm, move(client_data));
    auto attestation_ab = JS::ArrayBuffer::create(realm, move(attestation_object));
    auto raw_id_ab = JS::ArrayBuffer::create(realm, MUST(ByteBuffer::copy(credential_id)));

    auto response = AuthenticatorAttestationResponse::create(realm, client_data_ab, attestation_ab);
    return PublicKeyCredential::create(realm, Utf16String::from_utf8(id_b64), raw_id_ab, response, "platform"_string);
}

WebIDL::ExceptionOr<GC::Ref<PublicKeyCredential>> software_get_credential(JS::Realm& realm, JS::Object const& public_key_options)
{
    auto& vm = realm.vm();
    auto origin = HTML::current_settings_object().origin();

    auto challenge = TRY(buffer_from_js(realm, TRY(public_key_options.get("challenge"_utf16_fly_string))));

    ByteString rp_id;
    auto rp_id_value = TRY(public_key_options.get("rpId"_utf16_fly_string));
    if (!rp_id_value.is_undefined()) {
        auto string = TRY(WebIDL::to_byte_string(vm, rp_id_value));
        rp_id = to_byte_string(string);
    } else if (!origin.is_opaque()) {
        rp_id = to_byte_string(origin.host().serialize());
    } else {
        return WebIDL::NotAllowedError::create(realm, "Passkey RP ID missing"_utf16);
    }

    StoredPasskey* match = nullptr;
    for (auto& entry : passkey_store()) {
        if (entry.rp_id == rp_id) {
            match = &entry;
            break;
        }
    }
    if (!match)
        return WebIDL::NotAllowedError::create(realm, "No software passkey for this RP"_utf16);

    match->sign_count += 1;

    auto client_data = TRY(build_client_data_json(realm, "webauthn.get"sv, challenge, origin));
    auto client_hash = TRY(sha256(realm, client_data));
    auto auth_data = TRY(make_authenticator_data(realm, rp_id, match->credential_id, {}, false, match->sign_count));

    ByteBuffer to_sign;
    if (to_sign.try_append(auth_data).is_error() || to_sign.try_append(client_hash).is_error())
        return WebIDL::UnknownError::create(realm, "OOM"_utf16);
    auto hash = TRY(sha256(realm, to_sign));

    ::Crypto::Curves::SECP256r1 curve;
    auto private_key = ::Crypto::UnsignedBigInteger::import_data(match->private_key_bytes);
    auto signature_or_error = curve.sign(hash, private_key);
    if (signature_or_error.is_error())
        return WebIDL::UnknownError::create(realm, "Sign failed"_utf16);
    auto signature = signature_or_error.release_value();
    auto sig_der_or_error = signature.to_asn();
    if (sig_der_or_error.is_error())
        return WebIDL::UnknownError::create(realm, "Sign failed"_utf16);
    auto sig_der = sig_der_or_error.release_value();

    auto id_b64 = TRY(lift_string(realm, encode_base64url(match->credential_id, AK::OmitPadding::Yes)));
    auto client_data_ab = JS::ArrayBuffer::create(realm, move(client_data));
    auto auth_data_ab = JS::ArrayBuffer::create(realm, move(auth_data));
    auto signature_ab = JS::ArrayBuffer::create(realm, move(sig_der));
    auto user_handle_ab = JS::ArrayBuffer::create(realm, MUST(ByteBuffer::copy(match->user_handle)));
    auto raw_id_ab = JS::ArrayBuffer::create(realm, MUST(ByteBuffer::copy(match->credential_id)));

    auto response = AuthenticatorAssertionResponse::create(realm, client_data_ab, auth_data_ab, signature_ab, user_handle_ab);
    return PublicKeyCredential::create(realm, Utf16String::from_utf8(id_b64), raw_id_ab, response, "platform"_string);
}

}
