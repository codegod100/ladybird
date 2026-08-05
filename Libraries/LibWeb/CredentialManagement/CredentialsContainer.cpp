/*
 * Copyright (c) 2025, Altomani Gianluca <altomanigianluca@gmail.com>
 * Copyright (c) 2026, experimental password manager + passkey hooks
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Format.h>
#include <AK/Utf16String.h>
#include <LibJS/Runtime/ValueInlines.h>
#include <LibWeb/CredentialManagement/CredentialsContainer.h>
#include <LibWeb/CredentialManagement/OpenBaoStore.h>
#include <LibWeb/CredentialManagement/PasswordCredential.h>
#include <LibWeb/CredentialManagement/PasswordCredentialOperations.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/WebAuthn/PublicKeyCredential.h>
#include <LibWeb/WebAuthn/SoftwarePasskeyAuthenticator.h>
#include <LibWeb/WebIDL/DOMException.h>
#include <LibWeb/WebIDL/Promise.h>

namespace Web::CredentialManagement {

GC_DEFINE_ALLOCATOR(CredentialsContainer);

GC::Ref<CredentialsContainer> CredentialsContainer::create(JS::Realm& realm)
{
    return realm.create<CredentialsContainer>(realm);
}

CredentialsContainer::~CredentialsContainer() { }

static GC::Ref<WebIDL::Promise> reject_not_implemented(JS::Realm& realm, Utf16View const& name)
{
    return WebIDL::create_rejected_promise_from_exception(realm, realm.vm().throw_completion<JS::InternalError>(JS::ErrorType::NotImplemented, name));
}

static ByteString string_to_byte_string(String const& string)
{
    return ByteString { string.bytes() };
}

static ByteString utf16_to_byte_string(Utf16String const& string)
{
    return string_to_byte_string(string.to_well_formed_utf8());
}

static WebIDL::ExceptionOr<GC::Ref<PasswordCredential>> password_from_openbao(JS::Realm& realm, URL::Origin const& origin)
{
    auto origin_s = string_to_byte_string(origin.serialize());
    auto found = OpenBaoStore::find_password(origin_s);
    if (found.is_error())
        return WebIDL::NotAllowedError::create(realm, "OpenBao unavailable (set BAO_ADDR / OPENBAO_TOKEN)"_utf16);
    if (!found.value().has_value())
        return WebIDL::NotAllowedError::create(realm, "No password for this origin"_utf16);

    auto const& entry = *found.value();
    Bindings::PasswordCredentialData data;
    data.id = Utf16String::from_utf8(entry.username);
    data.password = Utf16String::from_utf8(entry.password);
    data.name = data.id;
    return create_password_credential(realm, data, origin);
}

static WebIDL::ExceptionOr<void> store_password_in_openbao(JS::Realm& realm, PasswordCredential const& credential)
{
    auto origin = string_to_byte_string(credential.origin().serialize());
    auto username = utf16_to_byte_string(credential.id());
    auto password = utf16_to_byte_string(credential.password());
    auto result = OpenBaoStore::store_password(origin, username, password);
    if (result.is_error())
        return WebIDL::NotAllowedError::create(realm, "Failed to store password in OpenBao"_utf16);
    return {};
}

// https://www.w3.org/TR/credential-management-1/#dom-credentialscontainer-get
GC::Ref<WebIDL::Promise> CredentialsContainer::get(Bindings::CredentialRequestOptions const& options)
{
    auto& realm = *vm().current_realm();

    dbgln("CredMan get: public_key={} password={}", options.public_key.has_value(), options.password);
    if (options.public_key.has_value() && options.public_key->is_object()) {
        dbgln("CredMan get: dispatching software_get_credential");
        auto result = WebAuthn::software_get_credential(realm, options.public_key->as_object());
        if (result.is_error()) {
            dbgln("CredMan get: software_get_credential failed");
            return WebIDL::create_rejected_promise_from_exception(realm, result.release_error());
        }
        dbgln("CredMan get: software_get_credential ok");
        return WebIDL::create_resolved_promise(realm, JS::Value(result.release_value()));
    }

    if (options.password) {
        auto origin = HTML::current_settings_object().origin();
        auto result = password_from_openbao(realm, origin);
        if (result.is_error())
            return WebIDL::create_rejected_promise_from_exception(realm, result.release_error());
        return WebIDL::create_resolved_promise(realm, JS::Value(result.release_value()));
    }

    dbgln("CredMan get: not implemented for these options");
    return reject_not_implemented(realm, "get"_utf16);
}

// https://www.w3.org/TR/credential-management-1/#dom-credentialscontainer-store
GC::Ref<WebIDL::Promise> CredentialsContainer::store(Credential const& credential)
{
    auto& realm = *vm().current_realm();

    if (credential.type() == "password"_utf16_fly_string) {
        auto const& password_credential = static_cast<PasswordCredential const&>(credential);
        auto result = store_password_in_openbao(realm, password_credential);
        if (result.is_error())
            return WebIDL::create_rejected_promise_from_exception(realm, result.release_error());
        // Platform objects must go through GC::Ref — JS::Value has no ctor from T&.
        return WebIDL::create_resolved_promise(realm, JS::Value(GC::Ref { const_cast<PasswordCredential&>(password_credential) }));
    }

    return reject_not_implemented(realm, "store"_utf16);
}

// https://www.w3.org/TR/credential-management-1/#dom-credentialscontainer-create
GC::Ref<WebIDL::Promise> CredentialsContainer::create(Bindings::CredentialCreationOptions const& options)
{
    auto& realm = *vm().current_realm();

    if (options.public_key.has_value() && options.public_key->is_object()) {
        auto result = WebAuthn::software_create_credential(realm, options.public_key->as_object());
        if (result.is_error())
            return WebIDL::create_rejected_promise_from_exception(realm, result.release_error());
        return WebIDL::create_resolved_promise(realm, JS::Value(result.release_value()));
    }

    if (options.password.has_value()) {
        auto origin = HTML::current_settings_object().origin();
        auto result = options.password->visit(
            [&](Bindings::PasswordCredentialData const& data) {
                return create_password_credential(realm, data, origin);
            },
            [&](GC::Ref<HTML::HTMLFormElement> form) {
                return create_password_credential(realm, form, origin);
            });
        if (result.is_error())
            return WebIDL::create_rejected_promise_from_exception(realm, result.release_error());
        auto credential = result.release_value();
        auto stored = store_password_in_openbao(realm, credential);
        if (stored.is_error())
            return WebIDL::create_rejected_promise_from_exception(realm, stored.release_error());
        return WebIDL::create_resolved_promise(realm, JS::Value(credential));
    }

    return reject_not_implemented(realm, "create"_utf16);
}

// https://www.w3.org/TR/credential-management-1/#dom-credentialscontainer-preventsilentaccess
GC::Ref<WebIDL::Promise> CredentialsContainer::prevent_silent_access()
{
    auto& realm = *vm().current_realm();
    return WebIDL::create_resolved_promise(realm, JS::js_undefined());
}

CredentialsContainer::CredentialsContainer(JS::Realm& realm)
    : PlatformObject(realm)
{
}

void CredentialsContainer::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(CredentialsContainer);
    Base::initialize(realm);
}

}
