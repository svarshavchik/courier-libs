/*
** Copyright 2022 S. Varshavchik.
** See COPYING for distribution information.
*/

#ifndef	maildirkeywords_h
#define	maildirkeywords_h

#include	"config.h"

#include	"maildircreate.h"
#include <set>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <tuple>
#include <list>
#include <utility>
#include <memory>
#include <functional>
#include <stdexcept>

namespace mail {
	namespace keywords {

#if 0
	}
}
#endif

extern const char *verbotten_chars;

// New implementation of keywords for C++11
//
// Like the old one, they're case-insensitive. Define a hash function and
// equality comparison.

struct keywordhash {
	size_t operator()(std::string s) const;
};

struct keywordeq {
	bool operator()(const std::string &, const std::string &) const;
};


//
// A collection of keywords for a given message.

typedef std::unordered_set<std::string, keywordhash, keywordeq> list;

// mail::keywords::hashtable<T> hashtable;
//
// mail::keyword::message<T> message;
//
// hashtable<>'s and message<>'s optional template parameter
// a metadata class that's associated with the keywords and their message.
// The hashtable is a reference-counted object. The default constructor
// creates a new hashtable. Copying the hashtable object creates another
// reference to the underlying hashtable.
//
// A default-constructed message is not linked to any hashtable, or has any
// keywords.
//
// message.keywords(hashtable, {"keyword1", "keyword2"});
//
// Defines the message's keywords. The 2nd parameter may be
// an empty unordered_set, defining no keywords. Additional
// variadic parameters get forwarded to the optional metadata
// class's constructor. Any existing keywords, and metadata
// that was associated with this message get replaced.
//
// A non-default message constructor is equivalent to default-constructing a
// new message and then using keywords() to link it to a hashtable.
//
// The list of keywords may be empty, but there's a difference between a
// default-constructed message and the one that's linked to the hashtable:
// the metadata. Linking a message to a hashtable, with an empty or a
// non-empty keyword list, instantiates the metadata object.
//
// message.reset() delinks the message object from the hashtable. Message
// objects are also reference-counted. The associated metadata gets destroyed
// only when the last message linked to it gets destroyed.
//
// message.enumerate([](const std::string &s){});
// mail::keywords::list keywords=message.keywords();
//
// Enumerates or returns the message's keywords.
//
//=========================================================================
//
// hashtable->enumerate_keywords([](const std::string &s){});
//
// Enumerates all keywords in the hash table.
//
// hashtable->enumerate_messages([](const T &metadata){});
//
// Enumerates all messages' metadata in the hash table.
//
// hashtable->messages("$Sent", [](const T &metadata){});
//
// Enumerates all metadata for messages in the hash table that have this
// keyword.

// Default template parameter, no metadata.

struct no_metadata {};

template<typename T> struct hashtable_entry;

// keywordtable: the reference-counted object that a hashtable refers to.
//
// The map's key is a keyword, and the maps contents are list of
// hashtable_entry that link the keyword to the metadata of every message
// that has this keyword.

template<typename T>
using keywordtable=std::unordered_map<
	std::string,
	std::list<hashtable_entry<T> *>,
	keywordhash,
	keywordeq>;

// Metadata of a message, and a list of all of its keyword, as keyword_entries
//
// The keywordtable uses direct pointers to the hashtable_entry object which
// are stored in the list i the metadata_entry.
//
// refcnt is the number of message objects that are pointing here.

template<typename T> struct metadata_entry {
	T value;
	std::list<hashtable_entry<T>> entries;
	size_t refcnt;

	template<typename ...Args>
	metadata_entry(Args && ...args)
		: value{std::forward<Args>(args)...}, refcnt{0}
	{
	}
};

template<typename T>
using metadatatable=std::list<metadata_entry<T>>;

template<typename T=no_metadata> struct hashtable;

// A hashtable entry links keywords with their messages. There's a hashtable
// entry for every keyword in every message.
//
// A message with multiple keywords will have multiple hashtable entries.
//
// A keyword that's used by multiple messages will have multiple
// hashtable entries.
//
// The hashtable entry consists of:
//
// - An iterator in the keywordtable, pointing to this keyword.
//
// - An iterator in the keyword's std::list's, in the keyword table, for the
//   pointer to this hashtable_entry.
//
// - An iterator in the metadata table for the message that has this keyword.
//
// - An iterator to itself, in the message's metadata table's std::list.

template<typename T> struct hashtable_entry {

	typename keywordtable<T>::iterator keyword;
	typename std::list<hashtable_entry<T> *
			   >::iterator keyword_entry;

	typename metadatatable<T>::iterator metadata;
	typename std::list<hashtable_entry<T>
			   >::iterator metadata_entry;

	// Common code for removing this keyword from this message
	//
	// This removes the link from the keywordtable to this hashtable
	// entry. If this is the last message with this keyword, the
	// entry in the keyword table gets removed.
	//
	// The code calling this takes care of delinking and removing
	// this hashtable entry from its metadata's list.

	void removing(const hashtable<T> &installed_hashtable)
	{
		keyword->second.erase(keyword_entry);

		if (keyword->second.empty())
			installed_hashtable->keywords
				.erase(keyword);
	}
};

// Keywords and metadata. This is the object a hashtable<> refers to.

template<typename T>
struct hashtable_base {

	keywordtable<T> keywords;

	metadatatable<T> metadata;

	template<typename Callback>
	void enumerate_keywords(Callback &&callback) const
	{
		for (auto &v: keywords)
			callback(v.first);
	}

	mail::keywords::list enumerate_keywords() const
	{
		mail::keywords::list ret;

		enumerate_keywords([&](const std::string &kw)
		{
			ret.insert(kw);
		});
		return ret;
	}

	template<typename Callback>
	void enumerate_messages(Callback &&callback)
	{
		for (auto &v: metadata)
			callback(v.value);
	}

	template<typename Key, typename Callback>
	void messages(Key &&key, Callback &&callback)
	{
		auto iter=keywords.find(
			std::forward<Key>(key)
		);

		if (iter == keywords.end())
			return;

		for (auto &e: iter->second)
			callback(e->metadata->value);
	}
};

void read_keywords_from_file(
	std::istream &i,
	const std::function<void (const std::string &,
				  mail::keywords::list &keywords)> &set);

// A reference-counted handle for a keywords/messages hashtable.

template<typename T>
struct hashtable : std::shared_ptr<hashtable_base<T>> {

	hashtable() : std::shared_ptr<hashtable_base<T>>{
		std::make_shared<hashtable_base<T>>()
			}
	{
	}

	using std::shared_ptr<hashtable_base<T>>::shared_ptr;

	using std::shared_ptr<hashtable_base<T>>::operator->;
	using std::shared_ptr<hashtable_base<T>>::operator bool;

	void load(
		const std::string &maildir,
		const std::function<std::string (const T&)>
		&getfilename,
		const std::function<bool (
			  const std::string &,
			  mail::keywords::list &keywords)> &set,
		const std::function<bool ()> &end_of_keywords
	);

	void save_keywords_to_file(
		FILE *fp,
		const std::function<std::string (const T&)> &getfilename
	);
};

// A message with keywords, and some metadata.

template<typename T=no_metadata> struct message {

protected:

	// This message's hashtable, and where its metadata and keywords
	// live.
	hashtable<T> installed_hashtable{nullptr};

	typename metadatatable<T>::iterator installed_metadata;

public:
	message()=default;

	operator bool() const
	{
		return !!installed_hashtable;
	}

	T &operator*()
	{
		if (!*this)
			throw std::runtime_error(
				"null pointer dereference"
			);

		return installed_metadata->value;
	}

	const T &operator*() const
	{
		if (!*this)
			throw std::runtime_error(
				"null pointer dereference"
			);

		return installed_metadata->value;
	}

	T &operator->()
	{
		if (!*this)
			throw std::runtime_error(
				"null pointer dereference"
			);

		return installed_metadata->value;
	}

	const T &operator->() const
	{
		if (!*this)
			throw std::runtime_error(
				"null pointer dereference"
			);

		return installed_metadata->value;
	}

	template<typename ...Args> message(
		const hashtable<T> &h,
		const list &strings={},
		Args &&...args)
	{
		keywords(h, strings,
			 std::forward<Args>(args)...);
	}

	message(const message<T> &m) : message()
	{
		operator=(m);
	}

	message &operator=(const message<T> &o)
	{
		auto h=o.installed_hashtable;
		auto m=o.installed_metadata;

		if (h)
			++m->refcnt;

		reset();

		installed_hashtable=h;
		installed_metadata=m;
		return *this;
	}

	~message()
	{
		reset();
	}

	void reset()
	{
		if (!installed_hashtable)
			return;

		if (--installed_metadata->refcnt)
		{
			// Just need to clear the pointer

			installed_hashtable=nullptr;
			return;
		}

		// Call removing() for each one of this message's hashtable
		// entries, to delink each one of the message's keywords.
		//
		// Then we erase our metadata entry.
		for (auto b=installed_metadata->entries.begin(),
			     e=installed_metadata->entries.end()
			     ;
		     b != e; ++b)
		{
			b->removing(installed_hashtable);
		}

		installed_hashtable->metadata.erase(
			installed_metadata
		);

		installed_hashtable=nullptr;
	}

	list keywords() const
	{
		list s;

		enumerate(
			[&]
			(const std::string &kw)
			{
				s.insert(kw);
			});

		return s;
	}

	template<typename Callback>
	void enumerate(Callback &&callback) const
	{
		if (!installed_hashtable)
			return;

		for (auto &entry:installed_metadata->entries)
		{
			callback(entry.keyword->first);
		}
	}

	template<typename ...Args>
	void keywords(const hashtable<T> &h,
		      const list &strings,
		      Args &&...args)
	{
		reset();

		// Link this message to a new hashtable, constructing the
		// metadata object. The initial list of keywords is empty
		// and we simply doadd() each one.

		installed_hashtable=h;

		if (!h)
			throw std::runtime_error(
				"null hashtable"
			);

		installed_hashtable->metadata.emplace_back(
			std::forward<Args>(args)...
		);
		installed_metadata=
			--installed_hashtable->metadata.end();

		++installed_metadata->refcnt;

		for (const auto &s:strings)
			doadd(s);
	}

	void add(const std::string &s)
	{
		if (!installed_hashtable)
			throw std::runtime_error(
				"keywords not installed"
			);

		// If this message already exists, remove it before adding
		// it, turning this situation into a slightly expensive
		// nothing-burger.
		remove(s);
		doadd(s);
	}

private:
	void doadd(const std::string &s)
	{
		// Create a new hashtable_entry

		installed_metadata->entries.emplace_back();

		auto new_entry=
			--installed_metadata->entries.end();

		// If this is the first message with this keyword we'll
		// emplace() an empty list into the keyword list, otherwise
		// we grab an iterator to the existing list.
		auto keyword=installed_hashtable
			->keywords.emplace(
				s,
				std::list<hashtable_entry<T> *>
				{}).first;

		// Link the keyword to the new hashtable_entry
		keyword->second.push_back(&*new_entry);

		new_entry->keyword=keyword;
		new_entry->keyword_entry=
			--keyword->second.end();

		// And link the metadata to the hashtable_entry.
		new_entry->metadata=installed_metadata;
		new_entry->metadata_entry=new_entry;
	}
public:
	void remove(const std::string &s)
	{
		if (!installed_hashtable)
			return;

		auto &list=installed_metadata->entries;

		// We just have to check this message's keywords, one by
		// one.

		for (auto b=list.begin(), e=list.end();
		     b != e; ++b)
		{
			if (b->keyword->first != s)
				continue;

			// We found it, delink it from the keywordtable,
			// then just drop the whole thing.

			b->removing(installed_hashtable);

			installed_metadata->entries.erase(b);
			break;
		}
	}

	bool empty() const {

		return !installed_hashtable ||
			installed_metadata->second.empty();
	}

	bool operator!=(const message<T> &o) const
	{
		return !operator==(o);
	}

	bool operator==(const message<T> &o) const
	{
		if (!installed_hashtable)
		{
			return !o.installed_hashtable;
		}

		if (!o.installed_hashtable)
			return false;

		return installed_metadata->value ==
			o.installed_metadata->value;
	}
};

#if 0
typedef hashtable<> Hashtable;
typedef message<> Message;
#endif

// Helper used for saving an updated :list of keywords.

struct save_keywords {
	std::unordered_map<std::string, std::string> &lookup;
	std::string tmpname;
	std::string newname;
	FILE *fp;

	save_keywords(
		std::unordered_map<std::string, std::string> &,
		FILE *);

	void operator()(std::string &filename,
			const list &keywords);
};

void load_impl(
	const std::string &maildir,
	const std::function<bool (
		       const std::string &,
		       mail::keywords::list &keywords)> &set,
	const std::function<bool ()> &end_of_keywords,
	const std::function<void (FILE *)> &save);

// Load messages' keywords from a maildir.
//
// h: the loaded keyword hash and metadata.
//
// getfilename: a caller-provided closure that retrieves
// the filename that's associated with a given message keyword
// metadata.
//
// set(): a callable object that receives a message name and
// its list of keywords. Returns true if the keywords were
// accepted, false if no such message exists (keywords are
// for a removed message).
//
// end_of_keywords() gets called after all messages' keywords
// were loaded. Returns true if the caller makes further
// changes to the keyword hashes and metadata.
//
// The :list of keywords gets updated if end_of_keywords()
// returns true, or if any call to set() returns false.

template<typename T>
void hashtable<T>::load(
	const std::string &maildir,
	const std::function<std::string (const T&)>
	&getfilename,
	const std::function<bool(const std::string &,
				 mail::keywords::list
				 &keywords)> &set,
	const std::function<bool ()> &end_of_keywords)
{
	load_impl(
		maildir,
		set,
		end_of_keywords,
		[&, this]
		(FILE *fp)
		{
			save_keywords_to_file(fp, getfilename);
		}
	);
}

// Helper when saving an updated list
// of keywords.
//
// This closure gets called when the
// load implementation has finished
// loading an updated list of keywords
// and determines that an updated
// master :list of keywords should
// be written and installed.
//
// We begin by extracting a list of
// all keywords that are now used,
// and placing them in a lookup map,
// initially empty.
//
// This is also used to capture the current keywords into a snapshot file.

template<typename T>
void hashtable<T>::save_keywords_to_file(
	FILE *fp,
	const std::function<std::string (const T&)> &getfilename
)
{
	// Create a lookup table for all keywords, and constructor the helper.

	std::unordered_map<std::string, std::string> keywords;

	auto &hash_meta= this->operator*();

	for (const auto &kw:hash_meta.keywords)
	{
		keywords.emplace(kw.first, "");
	}

	// The helper object creates a new
	// :list file, then enumerates every
	// keyword and numbers them.

	save_keywords helper{keywords, fp};

	// We assist the helper object by
	// going through the list of all
	// messages...

	for (const auto &meta:hash_meta.metadata)
	{
		list keywords;

		// Extracting each message's
		// keywords.
		for (const auto &entry:meta.entries)
		{
			keywords.insert(entry.keyword->first);
		}

		// And then saving them.

		auto filename=getfilename(meta.value);
		helper(filename, keywords);
	}
}

// Attempt to update the keywords

// true gets returned when the update succeeds
//
// false gets returned if there's another update is already
// scheduled. Run load() to pull it in, then update() can
// be tried again.

bool update(const std::string &maildir,
	    const std::string &filename,
	    const list &keywords);

/* BONUS: */

bool save_keywords_from(
	const std::string &maildir,
	const std::string &filename,
	const std::function<void (FILE *)> &saver,
	std::string &tmpname,
	std::string &newname,
	bool try_atomic);

#if 0
{
	{
#endif
	}
}

#endif
