/*
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <LibJS/Runtime/ArrayBuffer.h>
#include <LibWeb/Bindings/PublicKeyCredential.h>
#include <LibWeb/CredentialManagement/Credential.h>
#include <LibWeb/WebAuthn/AuthenticatorResponse.h>

namespace Web::WebAuthn {

class PublicKeyCredential final : public CredentialManagement::Credential {
    WEB_PLATFORM_OBJECT(PublicKeyCredential, CredentialManagement::Credential);
    GC_DECLARE_ALLOCATOR(PublicKeyCredential);

public:
    [[nodiscard]] static GC::Ref<PublicKeyCredential> create(JS::Realm&, Utf16String id, GC::Ref<JS::ArrayBuffer> raw_id, GC::Ref<AuthenticatorResponse> response, Optional<String> authenticator_attachment);

    virtual ~PublicKeyCredential() override;

    GC::Ref<JS::ArrayBuffer> raw_id() const { return *m_raw_id; }
    GC::Ref<AuthenticatorResponse> response() const { return *m_response; }
    Optional<String> authenticator_attachment() const { return m_authenticator_attachment; }
    Bindings::AuthenticationExtensionsClientOutputs get_client_extension_results() const { return {}; }

    static GC::Ref<WebIDL::Promise> is_user_verifying_platform_authenticator_available(JS::VM&);

    virtual Utf16FlyString const& type() const override;

private:
    PublicKeyCredential(JS::Realm&, Utf16String id, GC::Ref<JS::ArrayBuffer> raw_id, GC::Ref<AuthenticatorResponse> response, Optional<String> authenticator_attachment);
    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Cell::Visitor&) override;

    GC::Ref<JS::ArrayBuffer> m_raw_id;
    GC::Ref<AuthenticatorResponse> m_response;
    Optional<String> m_authenticator_attachment;
};

}
