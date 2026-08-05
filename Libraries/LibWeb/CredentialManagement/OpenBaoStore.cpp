/*
 * Experimental OpenBao KV v2 client for Ladybird CredMan / passkeys.
 * API shape mirrors https://github.com/codegod100/openbao-passkeys (src/lib/openbao.js).
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Base64.h>
#include <AK/Format.h>
#include <AK/HashMap.h>
#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <AK/JsonParser.h>
#include <AK/JsonValue.h>
#include <AK/Random.h>
#include <AK/StringBuilder.h>
#include <AK/Time.h>
#include <LibCore/Environment.h>
#include <LibCrypto/BigInt/UnsignedBigInteger.h>
#include <LibCrypto/Curves/SECPxxxr1.h>
#include <LibWeb/CredentialManagement/OpenBaoStore.h>
#include <curl/curl.h>

namespace Web::CredentialManagement {

namespace {

struct OpenBaoConfig {
    ByteString addr;
    ByteString token;
    ByteString kv_mount;
    ByteString passkeys_prefix;
    ByteString passwords_prefix;
};

// Process-local caches so Password Manager / CredMan do not re-fetch every open.
static constexpr i64 cache_ttl_ms = 5 * 60 * 1000;

struct ListCaches {
    Optional<Vector<PasswordEntry>> passwords;
    Optional<MonotonicTime> passwords_at;
    Optional<Vector<PasskeyEntry>> passkeys;
    Optional<MonotonicTime> passkeys_at;
    HashMap<ByteString, JsonObject> records; // "prefix\\0id" → KV data object
};

static ListCaches& caches()
{
    static ListCaches cache;
    return cache;
}

static bool cache_fresh(Optional<MonotonicTime> const& at)
{
    if (!at.has_value())
        return false;
    return (MonotonicTime::now() - *at).to_milliseconds() < cache_ttl_ms;
}

static ByteString record_cache_key(ByteString const& prefix, ByteString const& id)
{
    StringBuilder builder;
    builder.append(prefix);
    builder.append('\0');
    builder.append(id);
    return builder.to_byte_string();
}

static Optional<ByteString> env_get(StringView name)
{
    if (auto value = Core::Environment::get(name); value.has_value() && !value->is_empty())
        return ByteString { *value };
    return {};
}

static Optional<ByteString> env_first(StringView a, StringView b = {}, StringView c = {})
{
    if (auto value = env_get(a); value.has_value())
        return value;
    if (!b.is_empty()) {
        if (auto value = env_get(b); value.has_value())
            return value;
    }
    if (!c.is_empty()) {
        if (auto value = env_get(c); value.has_value())
            return value;
    }
    return {};
}

static ByteString trim_trailing_slashes(ByteString value)
{
    while (value.ends_with('/'))
        value = value.substring(0, value.length() - 1);
    return value;
}

static ByteString clean_segment(ByteString value)
{
    while (value.starts_with('/'))
        value = value.substring(1);
    while (value.ends_with('/'))
        value = value.substring(0, value.length() - 1);
    if (value.contains(".."sv))
        return {};
    return value;
}

static ErrorOr<OpenBaoConfig> load_config()
{
    OpenBaoConfig config;
    config.addr = trim_trailing_slashes(
        env_first("BAO_ADDR"sv, "OPENBAO_ADDR"sv, "VAULT_ADDR"sv)
            .value_or("http://127.0.0.1:8200"sv));
    auto token = env_first("OPENBAO_TOKEN"sv, "BAO_TOKEN"sv, "VAULT_TOKEN"sv);
    if (!token.has_value())
        return Error::from_string_literal("OpenBao token missing (set OPENBAO_TOKEN / BAO_TOKEN)");
    config.token = *token;
    config.kv_mount = clean_segment(env_first("OPENBAO_KV_MOUNT"sv).value_or("secret"sv));
    config.passkeys_prefix = clean_segment(env_first("OPENBAO_PASSKEYS_PREFIX"sv).value_or("passkeys"sv));
    config.passwords_prefix = clean_segment(env_first("OPENBAO_PASSWORDS_PREFIX"sv).value_or("passwords"sv));
    if (config.kv_mount.is_empty() || config.passkeys_prefix.is_empty() || config.passwords_prefix.is_empty())
        return Error::from_string_literal("Invalid OpenBao path configuration");
    return config;
}

static ByteString kv_data_path(OpenBaoConfig const& config, ByteString const& prefix, ByteString const& id)
{
    return ByteString::formatted("/v1/{}/data/{}/{}",
        clean_segment(config.kv_mount),
        clean_segment(prefix),
        clean_segment(id));
}

static ByteString kv_metadata_path(OpenBaoConfig const& config, ByteString const& prefix, ByteString const& id = {})
{
    if (id.is_empty()) {
        return ByteString::formatted("/v1/{}/metadata/{}",
            clean_segment(config.kv_mount),
            clean_segment(prefix));
    }
    return ByteString::formatted("/v1/{}/metadata/{}/{}",
        clean_segment(config.kv_mount),
        clean_segment(prefix),
        clean_segment(id));
}

static size_t curl_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* out = static_cast<ByteString*>(userdata);
    auto total = size * nmemb;
    StringBuilder builder;
    builder.append(*out);
    builder.append(StringView { ptr, total });
    *out = builder.to_byte_string();
    return total;
}

struct HttpResponse {
    long status { 0 };
    ByteString body;
};

static ErrorOr<HttpResponse> http_request(OpenBaoConfig const& config, ByteString const& method, ByteString const& path, Optional<ByteString> const& body = {})
{
    auto* curl = curl_easy_init();
    if (!curl)
        return Error::from_string_literal("curl_easy_init failed");

    auto url = ByteString::formatted("{}{}", config.addr, path);
    HttpResponse response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    auto token_header = ByteString::formatted("X-Vault-Token: {}", config.token);
    headers = curl_slist_append(headers, token_header.characters());

    curl_easy_setopt(curl, CURLOPT_URL, url.characters());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.characters());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);

    if (body.has_value()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body->characters());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body->length()));
    }

    auto code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        dbgln("OpenBao curl error: {} ({})", curl_easy_strerror(code), url);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return Error::from_string_literal("OpenBao HTTP request failed");
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

static ErrorOr<JsonValue> parse_json_or_empty(ByteString const& body)
{
    if (body.is_empty())
        return JsonValue {};
    return JsonParser::parse(body);
}

static ErrorOr<void> ensure_ok(HttpResponse const& response, char const* what)
{
    if (response.status >= 200 && response.status < 300)
        return {};
    dbgln("OpenBao {} failed: status={} body={}", what, response.status, response.body);
    if (response.status == 401 || response.status == 403)
        return Error::from_string_literal("OpenBao auth failed (check OPENBAO_TOKEN)");
    if (response.status == 404)
        return Error::from_string_literal("OpenBao path not found");
    return Error::from_string_literal("OpenBao request failed");
}

static ErrorOr<Vector<ByteString>> list_kv_keys(OpenBaoConfig const& config, ByteString const& prefix)
{
    auto path = ByteString::formatted("{}?list=true", kv_metadata_path(config, prefix));
    auto response = TRY(http_request(config, "GET"sv, path));
    if (response.status == 404)
        return Vector<ByteString> {};
    TRY(ensure_ok(response, "list"));
    auto json = TRY(parse_json_or_empty(response.body));
    if (!json.is_object())
        return Vector<ByteString> {};
    auto data = json.as_object().get_object("data"sv);
    if (!data.has_value())
        return Vector<ByteString> {};
    auto keys = data->get_array("keys"sv);
    if (!keys.has_value())
        return Vector<ByteString> {};

    Vector<ByteString> out;
    keys->for_each([&](JsonValue const& key_value) {
        if (!key_value.is_string())
            return;
        auto key = key_value.as_string().to_byte_string();
        if (key.ends_with('/'))
            key = key.substring(0, key.length() - 1);
        if (!key.is_empty())
            out.append(move(key));
    });
    return out;
}

static ErrorOr<Optional<JsonObject>> get_kv_record(OpenBaoConfig const& config, ByteString const& prefix, ByteString const& id)
{
    auto key = record_cache_key(prefix, id);
    if (auto cached = caches().records.get(key); cached.has_value())
        return JsonObject { *cached };

    auto response = TRY(http_request(config, "GET"sv, kv_data_path(config, prefix, id)));
    if (response.status == 404)
        return OptionalNone {};
    TRY(ensure_ok(response, "get"));
    auto json = TRY(parse_json_or_empty(response.body));
    if (!json.is_object())
        return OptionalNone {};
    auto outer = json.as_object().get_object("data"sv);
    if (!outer.has_value())
        return OptionalNone {};
    auto inner = outer->get_object("data"sv);
    if (!inner.has_value())
        return OptionalNone {};
    auto object = JsonObject { *inner };
    caches().records.set(key, object);
    return object;
}

static ErrorOr<void> put_kv_record(OpenBaoConfig const& config, ByteString const& prefix, ByteString const& id, JsonObject record)
{
    JsonObject data_wrapper;
    data_wrapper.set("data"sv, JsonValue { record });
    auto body = data_wrapper.serialized().to_byte_string();
    auto response = TRY(http_request(config, "POST"sv, kv_data_path(config, prefix, id), body));
    TRY(ensure_ok(response, "put"));
    caches().records.set(record_cache_key(prefix, id), move(record));
    caches().passwords.clear();
    caches().passwords_at.clear();
    caches().passkeys.clear();
    caches().passkeys_at.clear();
    return {};
}

static ErrorOr<void> delete_kv_record(OpenBaoConfig const& config, ByteString const& prefix, ByteString const& id)
{
    auto response = TRY(http_request(config, "DELETE"sv, kv_metadata_path(config, prefix, id)));
    if (response.status != 404)
        TRY(ensure_ok(response, "delete"));
    caches().records.remove(record_cache_key(prefix, id));
    caches().passwords.clear();
    caches().passwords_at.clear();
    caches().passkeys.clear();
    caches().passkeys_at.clear();
    return {};
}

static ByteString host_from_origin(ByteString const& origin)
{
    auto scheme = origin.find("://"sv);
    if (!scheme.has_value())
        return origin;
    auto start = *scheme + 3;
    auto end = origin.find('/', start).value_or(origin.length());
    auto hostport = origin.substring(start, end - start);
    if (auto at = hostport.find('@'); at.has_value())
        hostport = hostport.substring(*at + 1);
    return hostport;
}

static ByteString iso8601_now()
{
    return MUST(UnixDateTime::now().to_string("%Y-%m-%dT%H:%M:%SZ"sv, UnixDateTime::LocalTime::No)).to_byte_string();
}

static ByteString new_password_id()
{
    auto bytes = MUST(ByteBuffer::create_uninitialized(16));
    fill_with_random(bytes);
    return MUST(encode_base64url(bytes, AK::OmitPadding::Yes)).to_byte_string();
}

static String json_string(ByteString const& value)
{
    return String::from_utf8_without_validation(value.bytes());
}

static ByteString string_field(JsonObject const& object, StringView key)
{
    if (auto value = object.get_string(key); value.has_value())
        return value->to_byte_string();
    return {};
}

static u32 u32_field(JsonObject const& object, StringView key, u32 fallback = 0)
{
    if (auto value = object.get_u32(key); value.has_value())
        return *value;
    if (auto value = object.get_i32(key); value.has_value() && *value >= 0)
        return static_cast<u32>(*value);
    return fallback;
}

static ErrorOr<ByteBuffer> decode_b64_flexible(ByteString const& value)
{
    auto decoded = decode_base64url(StringView { value });
    if (!decoded.is_error())
        return decoded.release_value();
    return decode_base64(StringView { value });
}

static ErrorOr<ByteBuffer> pad_or_trim_scalar_32(ByteBuffer bytes)
{
    if (bytes.size() == 32)
        return bytes;
    if (bytes.size() > 32) {
        // Leading zero padding from some encoders.
        size_t start = 0;
        while (start < bytes.size() - 32 && bytes[start] == 0)
            ++start;
        if (bytes.size() - start != 32)
            return Error::from_string_literal("Invalid P-256 private scalar length");
        return TRY(bytes.slice(start, 32));
    }
    auto out = TRY(ByteBuffer::create_zeroed(32));
    out.overwrite(32 - bytes.size(), bytes.data(), bytes.size());
    return out;
}

static ErrorOr<ByteBuffer> private_key_bytes_from_jwk(JsonObject const& jwk)
{
    auto d = string_field(jwk, "d"sv);
    if (d.is_empty())
        return Error::from_string_literal("privateKeyJwk missing d");
    auto kty = string_field(jwk, "kty"sv);
    auto crv = string_field(jwk, "crv"sv);
    if (!kty.is_empty() && kty != "EC")
        return Error::from_string_literal("Unsupported privateKeyJwk kty");
    if (!crv.is_empty() && crv != "P-256")
        return Error::from_string_literal("Unsupported privateKeyJwk crv");
    return pad_or_trim_scalar_32(TRY(decode_b64_flexible(d)));
}

static ErrorOr<JsonObject> jwk_from_private_key_bytes(ByteBuffer const& private_key_bytes)
{
    auto scalar_bytes = TRY(pad_or_trim_scalar_32(TRY(ByteBuffer::copy(private_key_bytes))));
    ::Crypto::Curves::SECP256r1 curve;
    auto private_key = ::Crypto::UnsignedBigInteger::import_data(scalar_bytes);
    auto public_point = TRY(curve.generate_public_key(private_key));
    auto x = TRY(public_point.x_bytes());
    auto y = TRY(public_point.y_bytes());

    JsonObject jwk;
    jwk.set("kty"sv, JsonValue { "EC"_string });
    jwk.set("crv"sv, JsonValue { "P-256"_string });
    jwk.set("x"sv, JsonValue { TRY(encode_base64url(x, AK::OmitPadding::Yes)) });
    jwk.set("y"sv, JsonValue { TRY(encode_base64url(y, AK::OmitPadding::Yes)) });
    jwk.set("d"sv, JsonValue { TRY(encode_base64url(scalar_bytes, AK::OmitPadding::Yes)) });
    JsonArray key_ops;
    MUST(key_ops.append(JsonValue { "sign"_string }));
    jwk.set("key_ops"sv, JsonValue { move(key_ops) });
    jwk.set("ext"sv, JsonValue { true });
    return jwk;
}

static void copy_optional_string_field(JsonObject& dest, JsonObject const& src, StringView key)
{
    if (auto value = src.get_string(key); value.has_value())
        dest.set(key, JsonValue { *value });
}

static PasswordEntry password_from_record(JsonObject const& record, ByteString item_path)
{
    return PasswordEntry {
        .id = string_field(record, "id"sv),
        .origin = string_field(record, "origin"sv),
        .host = string_field(record, "host"sv),
        .username = string_field(record, "username"sv),
        .password = string_field(record, "password"sv),
        .item_path = move(item_path),
    };
}

static ErrorOr<PasskeyEntry> passkey_from_record(JsonObject const& record, ByteString item_path)
{
    auto rp_id = string_field(record, "rpId"sv);
    auto credential_id = string_field(record, "credentialId"sv);
    auto user_b64 = string_field(record, "userHandle"sv);
    if (rp_id.is_empty() || credential_id.is_empty())
        return Error::from_string_literal("Malformed OpenBao passkey record");

    ByteBuffer private_key_bytes;
    auto key_b64 = string_field(record, "privateKeyBytes"sv);
    if (key_b64.is_empty())
        key_b64 = string_field(record, "privateKey"sv);
    // Prefer JWK when present — empty privateKeyBytes can be left behind by older bugs.
    if (auto jwk = record.get_object("privateKeyJwk"sv); jwk.has_value() && jwk->has("d"sv)) {
        private_key_bytes = TRY(private_key_bytes_from_jwk(*jwk));
    } else if (!key_b64.is_empty()) {
        private_key_bytes = TRY(pad_or_trim_scalar_32(TRY(decode_b64_flexible(key_b64))));
    } else {
        return Error::from_string_literal("Passkey missing privateKeyBytes/privateKeyJwk");
    }

    ByteBuffer user_handle;
    if (!user_b64.is_empty())
        user_handle = TRY(decode_b64_flexible(user_b64));

    return PasskeyEntry {
        .rp_id = move(rp_id),
        .credential_id_b64 = move(credential_id),
        .user_handle = move(user_handle),
        .private_key_bytes = move(private_key_bytes),
        .sign_count = u32_field(record, "signCount"sv, 0),
        .item_path = move(item_path),
    };
}

}

ErrorOr<void> OpenBaoStore::store_password(ByteString const& origin, ByteString const& username, ByteString const& password)
{
    auto config = TRY(load_config());
    auto existing = TRY(list_passwords());
    ByteString id;
    ByteString created_at = iso8601_now();
    for (auto const& entry : existing) {
        if (entry.origin == origin && entry.username == username) {
            id = entry.id;
            break;
        }
    }
    if (id.is_empty()) {
        id = new_password_id();
    } else if (auto previous = TRY(get_kv_record(config, config.passwords_prefix, id)); previous.has_value()) {
        auto prior = string_field(*previous, "createdAt"sv);
        if (!prior.is_empty())
            created_at = prior;
    }

    JsonObject record;
    record.set("id"sv, JsonValue { json_string(id) });
    record.set("origin"sv, JsonValue { json_string(origin) });
    record.set("host"sv, JsonValue { json_string(host_from_origin(origin)) });
    record.set("url"sv, JsonValue { json_string(origin) });
    record.set("username"sv, JsonValue { json_string(username) });
    record.set("password"sv, JsonValue { json_string(password) });
    record.set("notes"sv, JsonValue { ""_string });
    record.set("createdAt"sv, JsonValue { json_string(created_at) });
    record.set("updatedAt"sv, JsonValue { json_string(iso8601_now()) });
    record.set("source"sv, JsonValue { "ladybird"_string });

    TRY(put_kv_record(config, config.passwords_prefix, id, move(record)));
    return {};
}

ErrorOr<Optional<PasswordEntry>> OpenBaoStore::find_password(ByteString const& origin, Optional<ByteString> const& username)
{
    auto listed = TRY(list_passwords());
    Optional<PasswordEntry> host_match;
    auto want_host = host_from_origin(origin);
    for (auto& entry : listed) {
        if (username.has_value() && entry.username != *username)
            continue;
        if (entry.origin == origin)
            return entry;
        auto entry_host = entry.host.is_empty() ? host_from_origin(entry.origin) : entry.host;
        if (!host_match.has_value() && !want_host.is_empty() && entry_host == want_host)
            host_match = entry;
    }
    return host_match;
}

ErrorOr<Vector<PasswordEntry>> OpenBaoStore::list_passwords()
{
    if (caches().passwords.has_value() && cache_fresh(caches().passwords_at))
        return *caches().passwords;

    auto config = TRY(load_config());
    auto keys = TRY(list_kv_keys(config, config.passwords_prefix));
    Vector<PasswordEntry> out;
    for (auto const& id : keys) {
        auto record = TRY(get_kv_record(config, config.passwords_prefix, id));
        if (!record.has_value())
            continue;
        auto path = ByteString::formatted("{}/data/{}/{}", config.kv_mount, config.passwords_prefix, id);
        auto entry = password_from_record(*record, path);
        if (entry.id.is_empty())
            entry.id = id;
        out.append(move(entry));
    }
    caches().passwords = out;
    caches().passwords_at = MonotonicTime::now();
    return out;
}

ErrorOr<void> OpenBaoStore::delete_password(ByteString const& origin, ByteString const& username)
{
    auto config = TRY(load_config());
    auto listed = TRY(list_passwords());
    for (auto const& entry : listed) {
        if (entry.origin == origin && entry.username == username) {
            if (entry.id.is_empty())
                return Error::from_string_literal("Password entry missing id");
            return delete_kv_record(config, config.passwords_prefix, entry.id);
        }
    }
    return {};
}

ErrorOr<void> OpenBaoStore::store_passkey(PasskeyEntry const& entry)
{
    if (entry.private_key_bytes.is_empty())
        return Error::from_string_literal("Refusing to store passkey with empty private key");

    auto config = TRY(load_config());
    auto user_b64 = TRY(encode_base64url(entry.user_handle, AK::OmitPadding::Yes));
    auto scalar_bytes = TRY(pad_or_trim_scalar_32(TRY(ByteBuffer::copy(entry.private_key_bytes))));
    // Reject the all-zero scalar (point at infinity) — usually means a move-after-use bug.
    bool all_zero = true;
    for (auto byte : scalar_bytes.bytes()) {
        if (byte != 0) {
            all_zero = false;
            break;
        }
    }
    if (all_zero)
        return Error::from_string_literal("Refusing to store all-zero P-256 private key");

    auto key_b64 = TRY(encode_base64url(scalar_bytes, AK::OmitPadding::Yes));
    auto jwk = TRY(jwk_from_private_key_bytes(scalar_bytes));

    JsonObject record;
    // Preserve extension metadata when bumping signCount.
    if (auto previous = TRY(get_kv_record(config, config.passkeys_prefix, entry.credential_id_b64)); previous.has_value()) {
        for (auto key : { "rpName"sv, "userName"sv, "userDisplayName"sv, "publicKeySpki"sv, "createdAt"sv, "aaguid"sv })
            copy_optional_string_field(record, *previous, key);
        if (auto transports = previous->get_array("transports"sv); transports.has_value())
            record.set("transports"sv, JsonValue { JsonArray { *transports } });
        if (auto backup = previous->get_bool("backupEligible"sv); backup.has_value())
            record.set("backupEligible"sv, JsonValue { *backup });
        if (auto backup = previous->get_bool("backupState"sv); backup.has_value())
            record.set("backupState"sv, JsonValue { *backup });
        if (auto source = previous->get_string("source"sv); source.has_value() && *source != "ladybird"sv)
            record.set("source"sv, JsonValue { *source });
    }

    record.set("credentialId"sv, JsonValue { json_string(entry.credential_id_b64) });
    record.set("rpId"sv, JsonValue { json_string(entry.rp_id) });
    record.set("userHandle"sv, JsonValue { user_b64 });
    record.set("privateKeyBytes"sv, JsonValue { key_b64 });
    record.set("privateKeyJwk"sv, JsonValue { move(jwk) });
    record.set("signCount"sv, JsonValue { entry.sign_count });
    if (!record.has("transports"sv)) {
        JsonArray transports;
        MUST(transports.append(JsonValue { "internal"_string }));
        record.set("transports"sv, JsonValue { move(transports) });
    }
    if (!record.has("source"sv))
        record.set("source"sv, JsonValue { "ladybird"_string });
    record.set("updatedAt"sv, JsonValue { json_string(iso8601_now()) });
    record.set("lastUsedAt"sv, JsonValue { json_string(iso8601_now()) });
    if (!record.has("createdAt"sv))
        record.set("createdAt"sv, JsonValue { json_string(iso8601_now()) });

    TRY(put_kv_record(config, config.passkeys_prefix, entry.credential_id_b64, move(record)));
    return {};
}

ErrorOr<Optional<PasskeyEntry>> OpenBaoStore::find_passkey(ByteString const& rp_id)
{
    auto all = TRY(list_passkeys());
    for (auto& entry : all) {
        if (entry.rp_id == rp_id)
            return entry;
    }
    return OptionalNone {};
}

ErrorOr<Vector<PasskeyEntry>> OpenBaoStore::list_passkeys()
{
    if (caches().passkeys.has_value() && cache_fresh(caches().passkeys_at))
        return *caches().passkeys;

    auto config = TRY(load_config());
    auto keys = TRY(list_kv_keys(config, config.passkeys_prefix));
    Vector<PasskeyEntry> out;
    for (auto const& id : keys) {
        auto record = TRY(get_kv_record(config, config.passkeys_prefix, id));
        if (!record.has_value())
            continue;
        auto path = ByteString::formatted("{}/data/{}/{}", config.kv_mount, config.passkeys_prefix, id);
        auto entry_or_error = passkey_from_record(*record, path);
        if (entry_or_error.is_error()) {
            dbgln("OpenBao: failed to decode passkey {}: {}", id, entry_or_error.error());
            continue;
        }
        out.append(entry_or_error.release_value());
    }
    caches().passkeys = out;
    caches().passkeys_at = MonotonicTime::now();
    return out;
}

ErrorOr<void> OpenBaoStore::delete_passkey(ByteString const& rp_id, ByteString const& credential_id_b64)
{
    auto config = TRY(load_config());
    auto record = TRY(get_kv_record(config, config.passkeys_prefix, credential_id_b64));
    if (record.has_value()) {
        auto stored_rp = string_field(*record, "rpId"sv);
        if (!stored_rp.is_empty() && stored_rp != rp_id)
            return Error::from_string_literal("Passkey rpId mismatch");
    }
    return delete_kv_record(config, config.passkeys_prefix, credential_id_b64);
}

void OpenBaoStore::invalidate_cache()
{
    caches().passwords.clear();
    caches().passwords_at.clear();
    caches().passkeys.clear();
    caches().passkeys_at.clear();
    caches().records.clear();
}

}
