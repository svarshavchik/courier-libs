/*
** Copyright 2003 S. Varshavchik.
** See COPYING for distribution information.
*/

#ifndef	maildirkeywords_h
#define	maildirkeywords_h

#include	"config.h"
#include	<stdio.h>
#include	<string.h>

#ifdef  __cplusplus
extern "C" {
#endif


/*
** IMAP keywords.  This data structure is designed so that it is possible to:
** A) Find all messages that have the given keyword,
** B) Find all keywords that are set for the given message,
** C) Optimize memory usage, even with many keywords set for many msgs.
*/

/* A doubly-linked list of keywords */

struct libmail_keywordEntry {
	struct libmail_keywordEntry *prev, *next; /* Doubly-linked list */


#define keywordName(ke) ((char *)((ke)+1))

	union {
		void *userPtr; /* Misc ptr */
		unsigned long userNum; /* Or misc num */
	} u;

	struct libmail_kwMessageEntry *firstMsg, *lastMsg;
	/* Doubly-linked list of messages that use this keyword */
};


/* The main keyword hash table */

struct libmail_kwHashtable {

	struct libmail_keywordEntry heads[43], tails[43];
	/* Dummy head/tail nodes for each hash bucket */

	int keywordAddedRemoved;
};

struct libmail_kwMessageEntry {
	struct libmail_kwMessageEntry *next, *prev;
	/* Doubly-linked list of all keywords set for this message */

	struct libmail_kwMessageEntry *keywordNext, *keywordPrev;
	/* Doubly-linked list of all entries for the same keyword */

	struct libmail_keywordEntry *libmail_keywordEntryPtr; /* The keyword */
	struct libmail_kwMessage *libmail_kwMessagePtr; /* The message */
};

struct libmail_kwMessage {

	struct libmail_kwMessageEntry *firstEntry, *lastEntry;
	/* Doubly-linked list of all keywords set for this message */

	union {
		void *userPtr; /* Misc ptr */
		unsigned long userNum; /* Or misc num */
	} u;
};

/*
** Initialize a libmail_kwHashtable
*/
void libmail_kwhInit(struct libmail_kwHashtable *);


/*
** Returns 0 if libmail_kwHashtable is empty.  Return -1, errno=EIO if it's
** not (sanity check).
*/
int libmail_kwhCheck(struct libmail_kwHashtable *);

/*
** Enumerate all defined keywords.
*/
int libmail_kwEnumerate(struct libmail_kwHashtable *h,
			int (*callback_func)(struct libmail_keywordEntry *,
					     void *),
			void *callback_arg);

/*
** Find a keyword in the hashtable, optionally create it.  If createIfNew,
** then we MUST add the returned result to some keywordMessage, else there'll
** be a cleanup problem.
*/
struct libmail_keywordEntry *
libmail_kweFind(struct libmail_kwHashtable *ht,
			 const char *name,
			 int createIfNew);


extern const char *libmail_kwVerbotten;
/*
** Optional list of chars prohibited in keyword names.  They get automagically
** replaced with underscores.
*/
extern int libmail_kwCaseSensitive;
/*
** Non zero if keyword names are case sensitive.
*/

/*
** Clear a reference to a particular keyword, from a particular message.
*/
void libmail_kweClear(struct libmail_keywordEntry *);

/*
** Create an abstract message object, with no keywords currently defined.
*/
struct libmail_kwMessage *libmail_kwmCreate();

/*
** Destroy the message object, automatically removing any keywords that were
** used by the object.
*/
void libmail_kwmDestroy(struct libmail_kwMessage *);

/*
** Link a keyword to a message.
*/
int libmail_kwmSet(struct libmail_kwMessage *, struct libmail_keywordEntry *);

/*
** Link a keyword to a message, by keyword's name.
*/
int libmail_kwmSetName(struct libmail_kwHashtable *,
		       struct libmail_kwMessage *, const char *);

/*
** Compare two messages, return 0 if they have the same keywords.
*/
int libmail_kwmCmp(struct libmail_kwMessage *,
		   struct libmail_kwMessage *);

/*
** Clear a keyword from a message.
*/
int libmail_kwmClearName(struct libmail_kwMessage *, const char *);
/*
** libmail_kwmClearName is for INTERNAL USE ONLY -- the name doesn't get vetted
** by libmail_kweFind.
*/

/*
** Clear a keyword from a message, the public version.
*/
int libmail_kwmClear(struct libmail_kwMessage *, struct libmail_keywordEntry *);
/*
**
*/
int libmail_kwmClearEntry(struct libmail_kwMessageEntry *);


/*****************************************************************************

The above is low-level stuff.  And now, a high level maildir storage API:

*****************************************************************************/

/*
** Read keywords from a maildir.  The application presumably has read and
** compiled a list of messages it found in the maildir's cur (and new)
** directories.
**
** The function maildir_kwRead() will now read the keywords associated with
** each message.  How the application maintains the list of messages in the
** maildir is irrelevant.  The application simply needs to allocate a pointer
** to a libmail_kwMessage structure, one pointer for each message in the
** maildir.  Each pointer must be initialized to a NULL, and the application
** provides a set of callback functions, as defined below, that return
** a pointer to this pointer (pay attention now), given the filename.
** maildir_kwRead() invokes the callback functions, as appropriate, while
** it's doing its business.
**
** There's other callback functions too, so let's get to business.
** The application must initialize the following structure before calling
** maildir_kwRead().  This is where the pointers to all callback functions
** are provided:
*/

struct maildir_kwReadInfo {

	struct libmail_kwMessage **(*findMessageByFilename)(const char
							    *filename,
							    int autocreate,
							    size_t *indexNum,
							    void *voidarg);
	/*
	** Return a pointer to a libmail_kwMessage * that's associated with
	** the message whose name is 'filename'.  'filename' will not have
	** :2, and the message's flags, so the application needs to be
	** careful.
	**
	** All libmail_kwMessage *s are initially NULL.  If autocreate is not
	** zero, the application must use libmail_kwmCreate() to initialize
	** the pointer, before returning.  Otherwise, the application should
	** return a pointer to a NULL libmail_kwMessage *.
	**
	** The application may use libmail_kwMessage.u for its own purposes.
	**
	** The application should return NULL if it can't find 'filename'
	** in its list of messages in the maildir.  That is a defined
	** possibility, and occur in certain race conditions (which are
	** properly handled, of course).
	**
	** If indexNum is not NULL, the application should set *indexNum to
	** the found message's index (if the application does not return NULL).
	** All messages the application has must be consecutively numbered,
	** beginning with 0 and up to, but not including, whatever the
	** application returns in getMessageCount().
	*/

	size_t (*getMessageCount)(void *voidarg);
	/*
	** The application should return the number of messages it thinks
	** there are in the maildir.
	*/

	struct libmail_kwMessage **(*findMessageByIndex)(size_t indexNum,
							 int autocreate,
							 void *voidarg);
	/*
	** This one is like the findMessageByFilename() callback, except that
	** instead of filename the applicationg gets indexNum which ranges
	** between 0 and getMessageCount()-1.
	** The return code from this callback is identical to the return code
	** from findMessageByFilename(), and autocreate's semantics are also
	** the same.
	*/

	const char *(*getMessageFilename)(size_t n, void *voidarg);
	/*
	** The application should return the filename for message #n.  The
	** application may or may not include :2, in the returned ptr.
	*/

	struct libmail_kwHashtable * (*getKeywordHashtable)(void *voidarg);
	/*
	** The application should return the libmail_kwHashtable that it
	** allocated to store all the keyword stuff.  Read keywords are
	** allocated from this hashtable.
	*/

	void (*updateKeywords)(size_t n, struct libmail_kwMessage *kw,
			       void *voidarg);
	/*
	** The updateKeywords callback gets invoked by maildir_kwRead()
	** if it needs to throw out the list of keywords it already read for
	** a given message, and replace it, instead, with another set of
	** keywords.  This can happen in certain circumstances.
	**
	** The application should retrieve the libmail_kwMessage pointer for
	** message #n.  It may or may not be null.  If it's not null, the
	** application must use libmail_kwmDestroy().  Then, afterwards,
	** the application should save 'kw' as the new pointer.
	**
	** This callback is provided so that the application may save whatever
	** it wants to save in kw->u.userPtr or kw->u.userNum, because 'kw'
	** was created by libmail_kwRead(), and not one of the two findMessage
	** callbacks.
	*/

	void *voidarg;
	/*
	** All of the above callbacks receive this voidarg as their last
	** argument.
	*/

	int tryagain;
	/*
	** libmail_kwRead() returns 0 for success, or -1 for a system failure
	** (check errno).
	**
	** If libmail_kwRead() returned 0, the application MUST check
	** tryagain.
	**
	** If tryagain is not 0, the application MUST:
	**     A) Take any non-NULL libmail_kwMessage pointers that are
	**        associated with each message in the maildir, use
	**        libmail_kwmDestroy() to blow them away, and reset each
	**        pointer to NULL.
	**
	**     B) Invoke libmail_kwRead() again.
	**
	** A non-0 tryagain indicates a recoverable race condition.
	*/


	/* Everything below is internal */

	int updateNeeded;
	int errorOccured;
};


int maildir_kwRead(const char *maildir,
		   struct maildir_kwReadInfo *rki);

/*
** maildir_kwSave saves new keywords for a particular message:
*/

int maildir_kwSave(const char *maildir, /* The maildir */
		   const char *filename,
		    /* The message.  :2, is allowed, and ignored */

		   struct libmail_kwMessage *newKeyword,
		    /* New keywords.  The ptr may be NULL */

		   char **tmpname,
		   char **newname,

		   int tryAtomic);

int maildir_kwSaveArray(const char *maildir,
			const char *filename,
			const char **flags,
			char **tmpname,
			char **newname,
			int tryAtomic);

/*
** maildir_kwSave returns -1 for an error.  If it return 0, it will initialize
** *tmpname and *newname, both will be full path filenames.  The application
** needs to simply call rename() with both filenames, and free() them, to
** effect the change.  Example:
**
**  char *tmpname, *newname;
**
**  if (maildir_kwSave( ..., &tmpname, &newname) == 0)
**  {
**         rename(tmpname, newname);
**
**         free(tmpname);
**         free(newname);
**  }
**
**  Of course, rename()s return code should also be checked.
**
**  If 'tryAtomic' is non-zero, the semantics are going to be slightly
**  different.  tryAtomic is non-zero when we want to update keywords
**  atomically.  To do that, first, use maildir_kwRead()  (or, most likely
**  maildir_kwgReadMaildir) to read the existing keywords, update the keywords
**  for the particular message, use maildir_kwSave(), but instead of rename()
**  use link().  Whether link succeeds or not, use unlink(tmpname) in any
**  case.  If link() failed with EEXIST, we had a race condition, so try
**  again.
**  Note - in NFS environments, must check not only that links succeeds, but
**  if stat-ing the tmpfile the number of links also must be 2.
*/

/*
** Set libmail_kwEnabled to ZERO in order to silently disable all maildir
** keyword functionality.  It's optional in Courier-IMAP.  Setting this flag
** to zero disables all actual keyword read/write functions, however all the
** necessary data still gets created (namely the courierimapkeywords
** subdirectory.
*/

extern int libmail_kwEnabled;


/*
** The following functions are "semi-internal".
**
** maildir_kwExport() uses the same struct maildir_kwReadInfo, to "export"
** the list of keywords assigned to all messages into a file.
**
** maildir_kwImport() imports the saved keyword list.
**
** These functions are meant to save a "snapshot" of the keywords into a
** flag file, nothing else.
*/

int maildir_kwExport(FILE *, struct maildir_kwReadInfo *);
int maildir_kwImport(FILE *, struct maildir_kwReadInfo *);


/****************************************************************************

Generic maildir_kwRead implementation.

****************************************************************************/

struct libmail_kwGeneric {

	struct libmail_kwHashtable kwHashTable;

	size_t nMessages;

	struct libmail_kwGenericEntry **messages;
	int messagesValid;

	struct libmail_kwGenericEntry *messageHashTable[99];
};

struct libmail_kwGenericEntry {

	struct libmail_kwGenericEntry *next; /* On the hash table */

	char *filename;
	size_t messageNum;
	struct libmail_kwMessage *keywords;
};

void libmail_kwgInit(struct libmail_kwGeneric *g);
int libmail_kwgDestroy(struct libmail_kwGeneric *g);
int libmail_kwgReadMaildir(struct libmail_kwGeneric *g,
			   const char *maildir);

struct libmail_kwGenericEntry *
libmail_kwgFindByName(struct libmail_kwGeneric *g, const char *filename);

struct libmail_kwGenericEntry *
libmail_kwgFindByIndex(struct libmail_kwGeneric *g, size_t n);

#ifdef  __cplusplus
}

#include <set>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <tuple>
#include <list>
#include <utility>
#include <memory>
#include <stdexcept>

/* Some C++ wrappers */

namespace mail {
	namespace keywords {

		class Hashtable {

		public:
			struct libmail_kwHashtable kwh;

			Hashtable();
			~Hashtable();

			Hashtable(const Hashtable &); /* UNDEFINED */
			Hashtable &operator=(const Hashtable &);
			/* UNDEFINED */
		};


		class MessageBase {
		public:
			struct libmail_kwMessage *km;
			size_t refCnt;

			MessageBase();
			~MessageBase();

			MessageBase(const MessageBase &); /* UNDEFINED */
			MessageBase &operator=(const MessageBase *);
			/* UNDEFINED */
		};

		class Message {

			MessageBase *b;

			bool copyOnWrite();

		public:
			Message();
			~Message();

			Message(const Message &);
			Message &operator=(const Message &);

			void getFlags(std::set<std::string> &) const;
			/* Extract list of flags */

			bool setFlags(Hashtable &,
				      const std::set<std::string> &);
			/* Set the flags. */

			bool addFlag(Hashtable &, std::string);
			bool remFlag(std::string);

			bool empty() const {
				return b->km == NULL
					|| b->km->firstEntry == NULL;
			}

			bool operator==(struct libmail_kwMessage *m) const {
				return b->km == NULL ?
					m == NULL || m->firstEntry == NULL:
					m && libmail_kwmCmp(b->km, m) == 0;
			}

			bool operator !=(struct libmail_kwMessage *m) const {
				return ! operator==(m);
			}

			void replace(struct libmail_kwMessage *p)
				{
					if (b->km)
						libmail_kwmDestroy(b->km);
					b->km=p;
				}

		};

		// New implementation of keywords for C++11

		typedef std::unordered_set<std::string> list;

		// mail::keywords::hashtable<> hashtable;
		//
		// mail::keyword::message<> message;
		//
		// hashtable<>'s and message<>'s optional template parameter
		// a metadata class that's associated with the keywords.
		//
		// message.keywords(hashtable, {"keyword1", "keyword2"});
		//
		// Defines the message's keywords. The 2nd parameter may be
		// an empty unordered_set, defining no keywords. Additional
		// variadic parameters get forwarded to the optional metadata
		// class's constructor. Any associated keywords, and metadata
		// get destroyed.
		//
		// hashtable->enumerate_keywords([](const std::string &s){});
		//
		// Enumerates all keywords in the hash table.
		//
		// hashtable->enumerate_messages([](const T &metadata){});
		//
		// Enumerates all messages in the hash table.
		//
		// hashtable->messages("$Sent", [](const T &metadata){});
		//
		// Enumerates all messages in the hash table that have this
		// keyword.
		//
		// message.enumerate([](const std::string &s){});
		// mail::keywords::list keywords=message.keywords();
		//
		// Enumerates or returns the message's keywords.

		struct no_metadata {
			bool operator==(const no_metadata &) const
			{
				return true;
			}

			bool operator!=(const no_metadata &) const
			{
				return false;
			}
		};

		template<typename T> struct hashtable_entry;

		template<typename T>
		using keywordtable=std::unordered_map<
			std::string,
			std::list<hashtable_entry<T> *>>;

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

		template<typename T> struct hashtable_entry {

			typename keywordtable<T>::iterator keyword;
			typename std::list<hashtable_entry<T> *
					   >::iterator keyword_entry;

			typename metadatatable<T>::iterator metadata;
			typename std::list<hashtable_entry<T>
					   >::iterator metadata_entry;


			void removing(const hashtable<T> &installed_hashtable)
			{
				keyword->second.erase(keyword_entry);

				if (keyword->second.empty())
					installed_hashtable->keywords
						.erase(keyword);
			}
		};

		template<typename T>
		struct hashtable_base {

			keywordtable<T> keywords;

			metadatatable<T> metadata;

			template<typename Callback>
			void enumerate_keywords(Callback &&callback)
			{
				for (auto &v: keywords)
					callback(v.first);
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
		};

		template<typename T=no_metadata> struct message {

		protected:
			hashtable<T> installed_hashtable{nullptr};

			typename metadatatable<T>::iterator installed_metadata;

		public:
			message()=default;

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

				remove();

				installed_hashtable=h;
				installed_metadata=m;
				return *this;
			}

			~message()
			{
				remove();
			}

			void remove()
			{
				if (!installed_hashtable)
					return;

				if (--installed_metadata->refcnt)
				{
					installed_hashtable=nullptr;
					return;
				}

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
				remove();

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

				remove(s);
				doadd(s);
			}

		private:
			void doadd(const std::string &s)
			{
				installed_metadata->entries.emplace_back();

				auto new_entry=
					--installed_metadata->entries.end();

				auto keyword=installed_hashtable
					->keywords.emplace(
						s,
						std::list<hashtable_entry<T> *>
						{}).first;

				keyword->second.push_back(&*new_entry);

				new_entry->keyword=keyword;
				new_entry->keyword_entry=
					--keyword->second.end();

				new_entry->metadata=installed_metadata;
				new_entry->metadata_entry=new_entry;
			}
		public:
			void remove(const std::string &s)
			{
				if (!installed_hashtable)
					return;

				auto &list=installed_metadata->entries;

				for (auto b=list.begin(), e=list.end();
				     b != e; ++b)
				{
					if (b->keyword->first != s)
						continue;

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
	}
}


/* BONUS: */

int maildir_kwSave(const char *maildir,
		   const char *filename,
		   const std::set<std::string> &keywords,

		   char **tmpname,
		   char **newname,

		   int tryAtomic);

#endif

#endif
