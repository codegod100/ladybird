/*
 * Experimental in-process software passkey authenticator (ES256 / "none" attestation).
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibJS/Forward.h>
#include <LibWeb/Forward.h>
#include <LibWeb/WebIDL/ExceptionOr.h>

namespace Web::WebAuthn {

class PublicKeyCredential;

// Create / get using options.publicKey as a JS object (see PublicKeyCredential.idl).
WebIDL::ExceptionOr<GC::Ref<PublicKeyCredential>> software_create_credential(JS::Realm&, JS::Object const& public_key_options);
WebIDL::ExceptionOr<GC::Ref<PublicKeyCredential>> software_get_credential(JS::Realm&, JS::Object const& public_key_options);

}
