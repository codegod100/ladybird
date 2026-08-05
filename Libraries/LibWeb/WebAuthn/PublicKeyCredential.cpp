/*
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/WebAuthn/PublicKeyCredential.h>
#include <LibWeb/WebIDL/Promise.h>

namespace Web::WebAuthn {

GC_DEFINE_ALLOCATOR(PublicKeyCredential);

GC::Ref<PublicKeyCredential> PublicKeyCredential::create(JS::Realm& realm, Utf16String id, GC::Ref<JS::ArrayBuffer> raw_id, GC::Ref<AuthenticatorResponse> response, Optional<String> authenticator_attachment)
{
    return realm.create<PublicKeyCredential>(realm, move(id), raw_id, response, move(authenticator_attachment));
}

PublicKeyCredential::PublicKeyCredential(JS::Realm& realm, Utf16String id, GC::Ref<JS::ArrayBuffer> raw_id, GC::Ref<AuthenticatorResponse> response, Optional<String> authenticator_attachment)
    : Credential(realm, move(id))
    , m_raw_id(raw_id)
    , m_response(response)
    , m_authenticator_attachment(move(authenticator_attachment))
{
}

PublicKeyCredential::~PublicKeyCredential() = default;

Utf16FlyString const& PublicKeyCredential::type() const
{
    static Utf16FlyString const type = "public-key"_utf16_fly_string;
    return type;
}

GC::Ref<WebIDL::Promise> PublicKeyCredential::is_user_verifying_platform_authenticator_available(JS::VM& vm)
{
    // Software "platform" authenticator — always available for demos.
    return WebIDL::create_resolved_promise(*vm.current_realm(), JS::Value(true));
}

void PublicKeyCredential::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(PublicKeyCredential);
    Base::initialize(realm);
}

void PublicKeyCredential::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_raw_id);
    visitor.visit(m_response);
}

}
