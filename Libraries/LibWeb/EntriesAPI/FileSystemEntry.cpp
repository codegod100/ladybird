/*
 * Copyright (c) 2024, Jamie Mansfield <jmansfield@cadixdev.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/FileSystemEntry.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/EntriesAPI/FileSystemEntry.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/HTML/WindowOrWorkerGlobalScope.h>
#include <LibWeb/WebIDL/AbstractOperations.h>
#include <LibWeb/WebIDL/CallbackType.h>

namespace Web::EntriesAPI {

GC_DEFINE_ALLOCATOR(FileSystemEntry);

GC::Ref<FileSystemEntry> FileSystemEntry::create(JS::Realm& realm, EntryType entry_type, Utf16String name)
{
    return realm.create<FileSystemEntry>(realm, entry_type, name);
}

FileSystemEntry::FileSystemEntry(JS::Realm& realm, EntryType entry_type, Utf16String name)
    : PlatformObject(realm)
    , m_entry_type(entry_type)
    , m_name(move(name))
{
}

void FileSystemEntry::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(FileSystemEntry);
    Base::initialize(realm);
}

// https://wicg.github.io/entries-api/#dom-filesystementry-isfile
bool FileSystemEntry::is_file() const
{
    // The isFile getter steps are to return true if this is a file entry and false otherwise.
    return m_entry_type == EntryType::File;
}

// https://wicg.github.io/entries-api/#dom-filesystementry-isdirectory
bool FileSystemEntry::is_directory() const
{
    // The isDirectory getter steps are to return true if this is a directory entry and false otherwise.
    return m_entry_type == EntryType::Directory;
}

// https://wicg.github.io/entries-api/#dom-filesystementry-name
Utf16String const& FileSystemEntry::name() const
{
    // The name getter steps are to return this's name.
    return m_name;
}

// https://wicg.github.io/entries-api/#dom-filesystementry-getparent
void FileSystemEntry::get_parent(GC::Ptr<WebIDL::CallbackType> success_callback, GC::Ptr<WebIDL::CallbackType> error_callback)
{
    auto& realm = this->realm();
    auto& global = HTML::relevant_global_object(*this);

    // AD-HOC: Ladybird only exposes synthetic FileSystemEntry objects from drag-and-drop today.
    // They have no parent directory in the backing store, so report null to the success callback.
    (void)error_callback;

    if (!success_callback)
        return;

    HTML::queue_global_task(HTML::Task::Source::FileReading, global, GC::create_function(realm.heap(), [success_callback] {
        auto result = WebIDL::invoke_callback(*success_callback, {}, { { JS::js_null() } });
        if (result.is_error())
            dbgln("FileSystemEntry::getParent: success callback threw an exception");
    }));
}

}
