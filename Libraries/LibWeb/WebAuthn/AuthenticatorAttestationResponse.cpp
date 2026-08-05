/*
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/WebAuthn/AuthenticatorAttestationResponse.h>

namespace Web::WebAuthn {

GC_DEFINE_ALLOCATOR(AuthenticatorAttestationResponse);

GC::Ref<AuthenticatorAttestationResponse> AuthenticatorAttestationResponse::create(JS::Realm& realm, GC::Ref<JS::ArrayBuffer> client_data_json, GC::Ref<JS::ArrayBuffer> attestation_object)
{
    return realm.create<AuthenticatorAttestationResponse>(realm, client_data_json, attestation_object);
}

AuthenticatorAttestationResponse::AuthenticatorAttestationResponse(JS::Realm& realm, GC::Ref<JS::ArrayBuffer> client_data_json, GC::Ref<JS::ArrayBuffer> attestation_object)
    : AuthenticatorResponse(realm, client_data_json)
    , m_attestation_object(attestation_object)
{
}

AuthenticatorAttestationResponse::~AuthenticatorAttestationResponse() = default;

void AuthenticatorAttestationResponse::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(AuthenticatorAttestationResponse);
    Base::initialize(realm);
}

void AuthenticatorAttestationResponse::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_attestation_object);
}

}
