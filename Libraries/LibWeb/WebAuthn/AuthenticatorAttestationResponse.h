/*
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Vector.h>
#include <LibWeb/Bindings/AuthenticatorAttestationResponse.h>
#include <LibWeb/WebAuthn/AuthenticatorResponse.h>

namespace Web::WebAuthn {

class AuthenticatorAttestationResponse final : public AuthenticatorResponse {
    WEB_PLATFORM_OBJECT(AuthenticatorAttestationResponse, AuthenticatorResponse);
    GC_DECLARE_ALLOCATOR(AuthenticatorAttestationResponse);

public:
    [[nodiscard]] static GC::Ref<AuthenticatorAttestationResponse> create(JS::Realm&, GC::Ref<JS::ArrayBuffer> client_data_json, GC::Ref<JS::ArrayBuffer> attestation_object);

    virtual ~AuthenticatorAttestationResponse() override;

    GC::Ref<JS::ArrayBuffer> attestation_object() const { return *m_attestation_object; }
    Vector<String> get_transports() const { return { "internal"_string }; }

private:
    AuthenticatorAttestationResponse(JS::Realm&, GC::Ref<JS::ArrayBuffer> client_data_json, GC::Ref<JS::ArrayBuffer> attestation_object);
    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Cell::Visitor&) override;

    GC::Ref<JS::ArrayBuffer> m_attestation_object;
};

}
