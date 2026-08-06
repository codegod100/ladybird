/*
 * Experimental OpenBao-backed password form autofill / save.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <AK/Function.h>
#include <AK/Utf16String.h>
#include <LibCore/EventLoop.h>
#include <LibGC/Root.h>
#include <LibThreading/ThreadPool.h>
#include <LibWeb/CredentialManagement/OpenBaoStore.h>
#include <LibWeb/CredentialManagement/PasswordAutofill.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/Element.h>
#include <LibWeb/DOM/Node.h>
#include <LibWeb/HTML/AttributeNames.h>
#include <LibWeb/HTML/HTMLFormElement.h>
#include <LibWeb/HTML/HTMLInputElement.h>
#include <LibWeb/TraversalDecision.h>

namespace Web::CredentialManagement {

namespace {

ByteString string_to_byte_string(String const& string)
{
    return ByteString { string.bytes() };
}

ByteString utf16_to_byte_string(Utf16String const& string)
{
    auto utf8 = string.to_well_formed_utf8();
    return string_to_byte_string(utf8);
}

bool attribute_tokens_contain(HTML::HTMLInputElement const& input, StringView needle)
{
    auto attr = input.attribute(HTML::AttributeNames::autocomplete);
    if (!attr.has_value() || attr->is_empty())
        return false;
    auto value = utf16_to_byte_string(*attr).to_lowercase();
    for (auto const& token : value.split(' ')) {
        if (token == needle)
            return true;
    }
    return false;
}

bool autocomplete_is_off(HTML::HTMLInputElement const& input)
{
    return attribute_tokens_contain(input, "off"sv);
}

bool looks_like_new_password(HTML::HTMLInputElement const& input)
{
    return attribute_tokens_contain(input, "new-password"sv);
}

bool name_or_id_suggests_username(HTML::HTMLInputElement const& input)
{
    auto haystack = ByteString::formatted("{} {}",
        utf16_to_byte_string(input.get_attribute_value(HTML::AttributeNames::name)),
        utf16_to_byte_string(input.get_attribute_value(HTML::AttributeNames::id)))
                        .to_lowercase();
    static constexpr StringView hints[] = {
        "user"sv, "email"sv, "login"sv, "identifier"sv, "account"sv, "handle"sv, "screen"sv
    };
    for (auto hint : hints) {
        if (haystack.contains(hint))
            return true;
    }
    return false;
}

bool is_username_candidate(HTML::HTMLInputElement const& input)
{
    if (!input.enabled())
        return false;
    if (autocomplete_is_off(input))
        return false;

    using Type = HTML::HTMLInputElement::TypeAttributeState;
    switch (input.type_state()) {
    case Type::Email:
        return true;
    case Type::Text:
    case Type::Search:
    case Type::Telephone:
    case Type::URL:
        break;
    default:
        return false;
    }

    if (attribute_tokens_contain(input, "username"sv) || attribute_tokens_contain(input, "email"sv))
        return true;
    return name_or_id_suggests_username(input);
}

bool is_fillable_password(HTML::HTMLInputElement const& input)
{
    if (!input.enabled())
        return false;
    if (input.type_state() != HTML::HTMLInputElement::TypeAttributeState::Password)
        return false;
    if (autocomplete_is_off(input) || looks_like_new_password(input))
        return false;
    return true;
}

bool field_is_empty(HTML::HTMLInputElement const& input)
{
    return input.value().is_empty();
}

void set_field_value(HTML::HTMLInputElement& input, ByteString const& value)
{
    if (value.is_empty())
        return;
    auto utf16 = Utf16String::from_utf8(value);
    (void)input.set_value(utf16.utf16_view());
}

struct LoginFields {
    GC::Ptr<HTML::HTMLInputElement> username;
    GC::Ptr<HTML::HTMLInputElement> password;
};

LoginFields find_login_fields_in_scope(DOM::Node& root, HTML::HTMLInputElement* preferred_password = nullptr)
{
    LoginFields fields;
    Vector<GC::Ref<HTML::HTMLInputElement>> usernames;
    Vector<GC::Ref<HTML::HTMLInputElement>> passwords;

    root.for_each_in_inclusive_subtree_of_type<HTML::HTMLInputElement>([&](auto& input) {
        if (is_fillable_password(input))
            passwords.append(input);
        else if (is_username_candidate(input))
            usernames.append(input);
        return TraversalDecision::Continue;
    });

    if (preferred_password && is_fillable_password(*preferred_password))
        fields.password = preferred_password;
    else if (!passwords.is_empty())
        fields.password = passwords.first();

    if (!fields.password)
        return fields;

    // Prefer a username candidate that precedes the password in tree order.
    for (auto& candidate : usernames) {
        auto position = candidate->compare_document_position(fields.password);
        if (position & DOM::Node::DOCUMENT_POSITION_FOLLOWING) {
            fields.username = candidate;
            break;
        }
    }
    if (!fields.username && !usernames.is_empty())
        fields.username = usernames.first();

    return fields;
}

LoginFields find_login_fields_for_password(HTML::HTMLInputElement& password)
{
    if (auto* form = password.form())
        return find_login_fields_in_scope(*form, &password);
    return find_login_fields_in_scope(password.root(), &password);
}

void fill_fields(LoginFields const& fields, PasswordEntry const& entry)
{
    if (fields.username && field_is_empty(*fields.username))
        set_field_value(*fields.username, entry.username);
    if (fields.password && field_is_empty(*fields.password))
        set_field_value(*fields.password, entry.password);
}

void fill_document_with_entry(DOM::Document& document, PasswordEntry const& entry)
{
    if (!document.is_fully_active())
        return;

    bool filled_any = false;
    document.for_each_in_inclusive_subtree_of_type<HTML::HTMLInputElement>([&](auto& input) {
        if (!is_fillable_password(input))
            return TraversalDecision::Continue;
        fill_fields(find_login_fields_for_password(input), entry);
        filled_any = true;
        return TraversalDecision::Break;
    });

    if (filled_any)
        dbgln("PasswordAutofill: filled login form for {}", entry.origin);
}

void fill_password_field_with_entry(HTML::HTMLInputElement& password, PasswordEntry const& entry)
{
    if (!password.document().is_fully_active())
        return;
    if (!is_fillable_password(password))
        return;

    auto fields = find_login_fields_for_password(password);
    if (!field_is_empty(password)) {
        if (fields.username && field_is_empty(*fields.username))
            set_field_value(*fields.username, entry.username);
        return;
    }

    fill_fields(fields, entry);
    dbgln("PasswordAutofill: filled from password field for {}", entry.origin);
}

void lookup_password_then(ByteString origin, Function<void(PasswordEntry const&)> apply)
{
    // Warm cache: stay on the content thread (no network).
    if (OpenBaoStore::password_cache_is_fresh()) {
        auto found = OpenBaoStore::find_password(origin);
        if (found.is_error()) {
            dbgln("PasswordAutofill: OpenBao lookup failed: {}", found.error());
            return;
        }
        if (auto entry = found.release_value(); entry.has_value())
            apply(*entry);
        return;
    }

    // Cold cache: curl on the thread pool, fill back on the content event loop.
    auto& main_loop = Core::EventLoop::current();
    Threading::ThreadPool::the().submit([origin = move(origin), apply = move(apply), &main_loop]() mutable {
        auto found = OpenBaoStore::find_password(origin);
        main_loop.deferred_invoke([found = move(found), apply = move(apply)]() mutable {
            if (found.is_error()) {
                dbgln("PasswordAutofill: OpenBao lookup failed: {}", found.error());
                return;
            }
            if (auto entry = found.release_value(); entry.has_value())
                apply(*entry);
        });
    });
}

}

void PasswordAutofill::try_fill_document(DOM::Document& document)
{
    if (!document.is_fully_active())
        return;
    if (document.origin().is_opaque())
        return;

    auto origin = string_to_byte_string(document.origin().serialize());
    auto document_root = GC::make_root(document);
    lookup_password_then(move(origin), [document_root](PasswordEntry const& entry) {
        fill_document_with_entry(*document_root, entry);
    });
}

void PasswordAutofill::try_fill_from_password_field(HTML::HTMLInputElement& password)
{
    if (!is_fillable_password(password))
        return;

    auto& document = password.document();
    if (document.origin().is_opaque())
        return;

    auto origin = string_to_byte_string(document.origin().serialize());
    auto password_root = GC::make_root(password);
    lookup_password_then(move(origin), [password_root](PasswordEntry const& entry) {
        fill_password_field_with_entry(*password_root, entry);
    });
}

void PasswordAutofill::maybe_save_from_form(HTML::HTMLFormElement& form)
{
    auto& document = form.document();
    if (document.origin().is_opaque())
        return;

    auto fields = find_login_fields_in_scope(form);
    if (!fields.password)
        return;

    auto password = utf16_to_byte_string(fields.password->value());
    if (password.is_empty())
        return;

    ByteString username;
    if (fields.username)
        username = utf16_to_byte_string(fields.username->value());
    if (username.is_empty())
        return;

    auto origin = string_to_byte_string(document.origin().serialize());
    // Persist off the content thread so submit never waits on OpenBao.
    Threading::ThreadPool::the().submit([origin = move(origin), username = move(username), password = move(password)] {
        auto result = OpenBaoStore::store_password(origin, username, password);
        if (result.is_error())
            dbgln("PasswordAutofill: save failed: {}", result.error());
        else
            dbgln("PasswordAutofill: saved credentials for {}", origin);
    });
}

}
