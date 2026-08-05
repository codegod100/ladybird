/*
 * Copyright (c) 2025, Altomani Gianluca <altomanigianluca@gmail.com>
 * Copyright (c) 2026, experimental software-passkey hook
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/ValueInlines.h>
#include <LibWeb/CredentialManagement/CredentialsContainer.h>
#include <LibWeb/WebAuthn/PublicKeyCredential.h>
#include <LibWeb/WebAuthn/SoftwarePasskeyAuthenticator.h>
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

// https://www.w3.org/TR/credential-management-1/#dom-credentialscontainer-get
GC::Ref<WebIDL::Promise> CredentialsContainer::get(Bindings::CredentialRequestOptions const& options)
{
    auto& realm = *vm().current_realm();

    if (options.public_key.has_value() && options.public_key->is_object()) {
        auto result = WebAuthn::software_get_credential(realm, options.public_key->as_object());
        if (result.is_error())
            return WebIDL::create_rejected_promise_from_exception(realm, result.release_error());
        return WebIDL::create_resolved_promise(realm, JS::Value(result.release_value()));
    }

    return reject_not_implemented(realm, "get"_utf16);
}

// https://www.w3.org/TR/credential-management-1/#dom-credentialscontainer-store
GC::Ref<WebIDL::Promise> CredentialsContainer::store(Credential const&)
{
    auto& realm = *vm().current_realm();
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

    return reject_not_implemented(realm, "create"_utf16);
}

// https://www.w3.org/TR/credential-management-1/#dom-credentialscontainer-preventsilentaccess
GC::Ref<WebIDL::Promise> CredentialsContainer::prevent_silent_access()
{
    auto& realm = *vm().current_realm();
    // No-op success for demos that call this.
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
