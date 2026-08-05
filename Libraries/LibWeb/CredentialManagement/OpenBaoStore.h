/*
 * Experimental OpenBao KV v2 backend for CredMan / software passkeys.
 * Layout inspired by https://github.com/codegod100/openbao-passkeys
 *
 * Env:
 *   BAO_ADDR / OPENBAO_ADDR / VAULT_ADDR  (default http://127.0.0.1:8200)
 *   OPENBAO_TOKEN / BAO_TOKEN / VAULT_TOKEN
 *   OPENBAO_KV_MOUNT                      (default secret)
 *   OPENBAO_PASSKEYS_PREFIX               (default passkeys)
 *   OPENBAO_PASSWORDS_PREFIX              (default passwords)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/ByteString.h>
#include <AK/Error.h>
#include <AK/Optional.h>
#include <AK/Vector.h>
#include <LibWeb/Export.h>

namespace Web::CredentialManagement {

struct WEB_API PasswordEntry {
    ByteString id;
    ByteString origin;
    ByteString host;
    ByteString username;
    ByteString password;
    ByteString item_path; // OpenBao KV path (for UI)
};

struct WEB_API PasskeyEntry {
    ByteString rp_id;
    ByteString credential_id_b64;
    ByteBuffer user_handle;
    ByteBuffer private_key_bytes;
    u32 sign_count { 0 };
    ByteString item_path;
};

// WEB_API: ladybird UI (PasswordManagerDialog) links these across the LibWeb DSO.
class WEB_API OpenBaoStore {
public:
    static ErrorOr<void> store_password(ByteString const& origin, ByteString const& username, ByteString const& password);
    static ErrorOr<Optional<PasswordEntry>> find_password(ByteString const& origin, Optional<ByteString> const& username = {});
    static ErrorOr<Vector<PasswordEntry>> list_passwords();
    static ErrorOr<void> delete_password(ByteString const& origin, ByteString const& username);

    static ErrorOr<void> store_passkey(PasskeyEntry const&);
    static ErrorOr<Optional<PasskeyEntry>> find_passkey(ByteString const& rp_id);
    static ErrorOr<Vector<PasskeyEntry>> list_passkeys();
    static ErrorOr<void> delete_passkey(ByteString const& rp_id, ByteString const& credential_id_b64);

    // Drop in-process list caches (Password Manager Refresh / after writes).
    static void invalidate_cache();
};

}
