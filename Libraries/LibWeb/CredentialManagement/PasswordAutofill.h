/*
 * Experimental OpenBao-backed password form autofill / save.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/Export.h>
#include <LibWeb/Forward.h>

namespace Web::CredentialManagement {

class WEB_API PasswordAutofill {
public:
    // Fill empty username/password fields for the document origin (if stored).
    static void try_fill_document(DOM::Document&);

    // Fill the login form containing this password field (focus / insert hooks).
    static void try_fill_from_password_field(HTML::HTMLInputElement&);

    // Persist username+password from a submitting form into OpenBao (silent).
    static void maybe_save_from_form(HTML::HTMLFormElement&);
};

}
