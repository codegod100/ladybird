/*
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibJS/Runtime/ArrayBuffer.h>
#include <LibWeb/Bindings/AuthenticatorResponse.h>
#include <LibWeb/Bindings/PlatformObject.h>

namespace Web::WebAuthn {

class AuthenticatorResponse : public Bindings::PlatformObject {
    WEB_PLATFORM_OBJECT(AuthenticatorResponse, Bindings::PlatformObject);
    GC_DECLARE_ALLOCATOR(AuthenticatorResponse);

public:
    virtual ~AuthenticatorResponse() override;

    GC::Ref<JS::ArrayBuffer> client_data_json() const { return *m_client_data_json; }

protected:
    AuthenticatorResponse(JS::Realm&, GC::Ref<JS::ArrayBuffer> client_data_json);
    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Cell::Visitor&) override;

    GC::Ref<JS::ArrayBuffer> m_client_data_json;
};

}
