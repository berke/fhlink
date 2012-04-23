// fhlink 1.0
//
// Copyright (C)2012 Berke DURAK
// Released under the GPL3 license

#include <vector>
#include <string>
#include <map>
#include <set>
#include <forward_list>
#include <vector>
#include <stdexcept>
#include <memory>
#include <algorithm>
#include <utility>

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <cinttypes>
#include <cstdarg>
#include <cassert>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>

using namespace std;

struct file_key {
	dev_t dev;
	ino_t ino;

	file_key(const dev_t &Dev, const ino_t &Ino) : dev(Dev), ino(Ino) { }

	bool operator<(const struct file_key &b) const {
		return dev < b.dev || (dev == b.dev && ino < b.ino);
		return true;
	}
};

struct file_id {
	dev_t dev;
	off_t size;

	bool operator<(const struct file_id &b) const {
		return size < b.size || (size == b.size && dev < b.dev);
	}
};

class non_copyable
{
protected:
	non_copyable() { }
	~non_copyable() { }
private:
	non_copyable(const non_copyable&);
	non_copyable &operator=(const non_copyable&);
};

class path {
	string u;

public:
	path() { }
	virtual ~path() { }

	string& get() { return u; }

	void set(const char *v) {
		u = v;
	}

	void push(const char *v) {
		if (*v) {
			if (u.size() > 0 && u[u.size() - 1] != '/' &&
				v[0] != '/')
				u += '/';
			u += v;
		}
	}

	void pop() {
		size_t pos = u.find_last_of('/');
		if (pos == 0 || pos == string::npos)
			throw runtime_error("Path empty");
		u.resize(pos);
	}
};

class string_pool : non_copyable {
	vector<char> pool;
	size_t count;

public:
	class handle {
		unsigned offset;
		friend class string_pool;
	};

	string_pool() : count(0) { }

	handle add(const char *u) {
		size_t m = strlen(u);
		unsigned i = pool.size();
		size_t n = pool.capacity();
		count ++;
		if (n < i + m + 1) {
			if (!n) n = 1;
			while (n < i + m + 1)
				n <<= 1;
			pool.reserve(n);
		}
		pool.resize(i + m + 1);
		memcpy(&pool[i], u, m + 1);
		handle h;
		h.offset = i;
		return h;
	}

	const char *get(const handle &h) const {
		return &pool[h.offset];
	}
};

struct file_info {
	string_pool::handle name;
	mode_t mode;
	ino_t ino;
	const file_info *parent;
	
	void clear() {
		mode = 0;
		parent = NULL;
	}

	string get_path(const string_pool &sp) {
		path p;

		make_path(sp, p, this);
		return p.get();
	}

public:
	void make_path(const string_pool &sp, path& p, const file_info *up)
	{
		if (up->parent != NULL) {
			make_path(sp, p, up->parent);
			p.push(sp.get(up->name));
		}
	}
};

class unix_rc {
public:
	unix_rc(int Rc) {
		check(Rc);
	}

	unix_rc() { }

	virtual ~unix_rc() { }

	int operator=(int Rc) {
		check(Rc);
		return Rc;
	}

	static void check(int Rc) {
		if (Rc < 0)
			error(NULL);
	}

	static void error(const char *detail) {
		throw runtime_error(
				string("Unix error: ")
				+ strerror(errno)
				+ (detail ?
					(string(" (") + detail + string(")")) :
					""));
	}
};

namespace fmt {
	static void vfpf(FILE *out, const char *fmt, va_list ap) {
		unix_rc rc = ::vfprintf(out, fmt, ap);
	}

	static void pf(const char *fmt, ...) {
		va_list ap;
		va_start(ap, fmt);
		vfpf(stdout, fmt, ap);
		va_end(ap);
	}

	static void fpf(FILE *out, const char *fmt, ...) {
		va_list ap;
		va_start(ap, fmt);
		vfpf(out, fmt, ap);
		va_end(ap);
	}

	static void fpc(char c, FILE *out) {
		fputc(c, out);
	}

	void print_quoted(FILE *out, const char *u)
	{
		fputc('\'', out);
		while (*u) {
			char c = *(u ++);
			if (32 <= c && c < 127) {
				if (c == '\'')
					fpf(out, "'\\''");
				else fpc(c, out);
			} else switch (c) {
				case '\n': fpf(out, "\\n"); break;
				case '\r': fpf(out, "\\r"); break;
				case '\b': fpf(out, "\\b"); break;
				default:
					   fpf(out, "\\%03o",
							   (unsigned int) c);
					   break;
			}
		}
		fpc('\'', out);
	}
};

struct talk_control {
	virtual bool info_enabled() const = 0;
	virtual bool warnings_enabled() const = 0;
};

class talk : non_copyable {
	const talk_control &ctrl;
	const char *progname;

public:
	talk(const talk_control &Ctrl, const char *Progname) :
		ctrl(Ctrl),
		progname(Progname)
	{ }

	void info(const char *fmt, ...) const {
		if (!ctrl.info_enabled()) return;

		fmt::fpf(stderr, "%s: ", progname);
		va_list ap;
		va_start(ap, fmt);
		fmt::vfpf(stderr, fmt, ap);
		fmt::fpf(stderr, "\n");
		va_end(ap);
	}

	void warning(const char *fmt, ...) const {
		if (!ctrl.warnings_enabled()) return;

		fmt::fpf(stderr, "%s: WARNING - ", progname);
		va_list ap;
		va_start(ap, fmt);
		fmt::vfpf(stderr, fmt, ap);
		fmt::fpf(stderr, "\n");
		va_end(ap);
	}
};

class formatter {
public:
	static string sprintf(const char *fmt, ...) {
		char *u;
		va_list ap;
		int n;

		va_start(ap, fmt);
		n = vasprintf(&u, fmt, ap);
		va_end(ap);

		if (n < 0)
			throw runtime_error("vasprintf");

		string v(u, n);
		free(u);
		return v;
	}
};

class unix_fd : non_copyable {
	int fd;

public:
	explicit unix_fd(int Fd) : fd(Fd) {
		unix_rc rc = fd;
	}

	virtual ~unix_fd() {
		if (fd >= 0) {
			unix_rc rc = close(fd);
			fd = -1;
		}
	}

	operator int() const {
		return fd;
	}

	int get() const { return fd; }
};

class unix_dir : non_copyable {
	DIR *dir;
public:
	unix_dir(const char *path) {
		dir = opendir(path);
		if (dir == NULL)
			throw runtime_error(
					string("Cannot open '") +
					path +
					string("': ") +
					strerror(errno));
	}
	virtual ~unix_dir() {
		unix_rc rc = closedir(dir);
	}
	bool read(const struct dirent *&e) {
		struct dirent *e_p = readdir(dir);
		if (e_p != NULL) {
			e = e_p;
			return true;
		} else return false;
	}
};

class lazy_string : non_copyable {
public:
	virtual ~lazy_string() { }
	virtual const char *get(void) = 0;
};

class time_value {
	struct timeval tv;
public:
	time_value() {
		clear();
	}

	void now() {
		unix_rc rc = gettimeofday(&tv, NULL);
	}

	void clear() {
		tv.tv_sec = 0;
		tv.tv_usec = 0;
	}

	bool is_null() {
		return tv.tv_sec == 0 && tv.tv_usec == 0;
	}

	int64_t microseconds(void) const {
		int64_t t = tv.tv_sec;
		t *= 1000000LL;
		t += tv.tv_usec;
		return t;
	}
};

struct tickable {
	virtual bool tick(uint64_t delta) = 0;
};

class progress : public tickable, non_copyable {
	FILE *out;
	time_value t_last, t_last_tick, t;
	uint64_t count;
	uint64_t maximum;
	uint64_t ticks;
	int columns;
	vector<char> buffer;
	bool line_available;
	bool is_tty;
	int64_t interval_us;
	const int64_t mask_min;
	int64_t mask;
	const int64_t mask_max;
	lazy_string &lstr;
	unsigned shift;
	bool enabled;

public:
	progress(FILE *Out, lazy_string &Lstr, unsigned Shift,
			bool Enabled) :
		out(Out),
		count(0),
		maximum(0),
		ticks(0),
		columns(80),
		buffer(columns),
		line_available(false),
		interval_us(100000),
		mask_min(1),
		mask(1),
		mask_max(65535),
		lstr(Lstr),
		shift(Shift),
		enabled(Enabled)
	{
		is_tty = enabled && isatty(2);
	}

	virtual ~progress() { }

	void reset(uint64_t Maximum=0, unsigned Shift=0) {
		count = 0;
		ticks = 0;
		shift = Shift;
		maximum = Maximum;
		t.clear();
	}

	bool tick(uint64_t delta) {
		if (!is_tty) return false;

		count += delta;

		if ((ticks ++) & mask) return false;
		t.now();

		if (!t_last_tick.is_null()) {
			int64_t delta_t_last = t.microseconds() -
				t_last_tick.microseconds();
			if (delta_t_last > 2 * interval_us / 10)
				mask >>= 1;
			else if (delta_t_last < interval_us / 2 / 10)
				mask = (mask << 1) | 1;
			mask = min(max(mask, mask_min), mask_max);
		}
		t_last_tick = t;
		
		if (t.microseconds() < t_last.microseconds() + interval_us)
			return false;
		t_last = t;
		show(lstr.get());
		return true;
	}

	void finish(const char *line) {
		if (is_tty) {
			if (line_available)
				fmt::fpf(out, "\r\033[1A");

			fmt::fpf(out, "%s\033[K\n", line);
			occupied();
		}
	}

	void occupied() { line_available = false; }

	virtual void show(const char *line) {
		int line_len = strlen(line);

		const int count_len = 16;

		if (line_available)
			fmt::fpf(out, "\r\033[1A");

		line_available = true;

		int available_for_line = columns - 17;

		if (columns < count_len + 4) {
			for (int i = 0; i < columns; i ++)
				fmt::fpf(out, ".");
		} else {
			if (maximum)
				fmt::fpf(out, "%7"PRIu64"/%7"PRIu64" ",
					count >> shift, maximum >> shift);
			else
				fmt::fpf(out, "%16"PRIu64" ", count >> shift);

			if (line_len > available_for_line) {
				line = line + line_len - available_for_line - 3;
				fmt::fpf(out, "...%s", line);
			} else {
				fmt::fpf(out, "%s", line);
			}
		}

		fmt::fpf(out, "\033[K\n");
		fflush(out);
	}
};

class lcg {
	uint32_t q;

public:
	lcg() {
		time_value t;
		t.now();
		uint64_t us = t.microseconds();
		q = (us >> 32) ^ us;
	}

	lcg(uint32_t Q) : q(Q) { }

	uint32_t get() {
		uint32_t x = q;
		q = q * 1664525 + 1013904223;
		return x;
	}
};

namespace file_utils
{
	static ssize_t really_read(const char *path,
			int fd, void *buffer, ssize_t m)
	{
		char *buffer_p = reinterpret_cast<char *>(buffer);
		ssize_t n = 0;

		while (m > 0) {
			ssize_t r = read(fd, buffer_p, m);
			if (r < 0) {
				if (errno == EINTR) continue;
				unix_rc::error(path);
			}
			if (!r) break;
			n += r;
			m -= r;
			buffer_p += r;
		}

		return n;
	}

	__attribute__((unused))
	static bool is_eof(const char *path, int fd) {
		char buf;

		while (true) {
			ssize_t r = read(fd, &buf, 1);
			if (r < 0) {
				if (errno == EINTR) continue;
				unix_rc::error(path);
			}
			return r ? false : true;
		}
	}

	static int compare(const char *path1, const char *path2,
			tickable *tck=NULL) {
		const int buffer_size = 524288;
		vector<uint8_t> buffer1(buffer_size), buffer2(buffer_size);
		ssize_t m1, m2;

		unix_fd fd1(open(path1, O_RDONLY));
		unix_fd fd2(open(path2, O_RDONLY));

		while (true) {
			m1 = really_read(path1, fd1, &buffer1[0], buffer_size);
			if (tck) tck->tick(m1);
			m2 = really_read(path2, fd2, &buffer2[0], buffer_size);
			if (tck) tck->tick(m2);
			if (m1 != m2) return m1 ? -1 : 1;
			if (!m1) return 0;
			int c = memcmp(&buffer1[0], &buffer2[0], m1);
			if (c) return c;
		}
	}

	static void decompose(const string &path, string &dir, string &base)
	{
		size_t i = path.find_last_of('/');

		if (i == string::npos) {
			dir.clear();
			base = path;
			return;
		}

		dir = path.substr(0, i);
		base = path.substr(i + 1, base.size() - i - 1);
	}

	static string find_backup_name(const string &path) {
		string dir, base;

		decompose(path, dir, base);
		size_t m = min(size_t(FILENAME_MAX - 9), base.size());
		base = base.substr(0, m);

		static lcg g;

		uint32_t i = 0;
		char buf[14];
		string u;
		struct stat st;
		int rc;

		do {
			do {
				snprintf(buf, sizeof(buf), ".bak.%08x", g.get());
				u = dir + "/" + base + buf;
				rc = lstat(u.c_str(), &st);
				if (rc < 0) {
					if (errno == ENOENT)
						return u;
					unix_rc rc2(rc);
				}
			} while (++ i);
		} while(base.size() > 0);

		throw std::runtime_error(
				string("Can't find backup name for '") +
				path +
				string("'"));
	}

	static void hard_link(
			const string &source,
			const vector<string> &targets,
			progress &pg,
			const talk &talker) {
		int rc;
		unsigned i;
		string t_i_bak;

		vector<string> backup_names(targets.size());

		for (i = 0; i < targets.size(); i ++) {
			const string &t_i = targets[i];
			t_i_bak = backup_names[i] = find_backup_name(t_i);

			pg.tick(1);

			rc = rename(t_i.c_str(), t_i_bak.c_str());
			if (rc < 0) {
				talker.warning("Skipping: can't rename "
					"'%s' to '%s': %s",
					t_i.c_str(), t_i_bak.c_str(),
					strerror(errno));
				pg.occupied();
				if (i) do {
					i --;
					const string t_i = targets[i];
					t_i_bak = backup_names[i];
					rc = rename(t_i_bak.c_str(),
							t_i.c_str());
					if (rc < 0) {
						talker.warning(
							"Furthermore, can't "
							"rename "
							"'%s' to '%s': %s",
							t_i_bak.c_str(),
							t_i.c_str(),
							strerror(errno));
						pg.occupied();
					}
				} while(i > 0);
				return;
			}
		}

		for (i = 0; i < targets.size(); i ++) {
			string t_i = targets[i];
			t_i_bak = backup_names[i];
			rc = link(source.c_str(), t_i.c_str());
			if (rc < 0) {
				talker.warning(
					"Warning: can't link "
					"'%s' to '%s': %s",
					source.c_str(), t_i.c_str(),
					strerror(errno));
				pg.occupied();

				rc = rename(t_i.c_str(), t_i_bak.c_str());
				if (rc < 0) {
					talker.warning(
						"Warning: can't restore "
						"'%s' to '%s': %s",
						t_i.c_str(), t_i_bak.c_str(),
						strerror(errno));
				}
			} else {
				rc = remove (t_i_bak.c_str());
				if (rc < 0) {
					talker.warning(
						"Warning: can't remove "
						"'%s': %s",
						t_i_bak.c_str(),
						strerror(errno));
					pg.occupied();
				}
			}
		}
	}
};

class checksummer
{
	enum { buffer_size_steps = 256 };

public:
	checksummer() { }

	uint64_t checksum(const char *path)
	{
		const int
			step_size_words = 7,
			step_size_bytes = step_size_words * sizeof(uint64_t);
		uint64_t buffer[buffer_size_steps * step_size_words];
		uint64_t a, b, c;

		unix_fd fd(open(path, O_RDONLY));

		a = b = c = 0;

		while (true) {
			ssize_t n = file_utils::really_read(path, fd, buffer,
					sizeof(buffer));

			if (n == 0) break;

			ssize_t nr = n % step_size_bytes;
			if (nr) {
				char *b = reinterpret_cast<char *>(buffer);
				memset(b + n, 0, step_size_bytes - nr);
			}

			unsigned n_steps = (n + nr) / step_size_bytes;

			uint64_t *p = buffer;

			while (n_steps --) {
				a += c;
				a += *(p ++); b = (b << 53) | (b >> 11); b += a;
				a ^= *(p ++); b = (b << 53) | (b >> 11); b += a;
				a -= *(p ++); b = (b << 53) | (b >> 11); b += a;
				a ^= *(p ++); b = (b << 53) | (b >> 11); b += a;
				a += *(p ++); b = (b << 53) | (b >> 11); b += a;
				a ^= *(p ++); b = (b << 53) | (b >> 11); b += a;
				a -= *(p ++); b = (b << 53) | (b >> 11); b += a;
				c += b;
			}
		}

		return c;
	}
};

class filename_filter {
public:
	virtual ~filename_filter() { }
	virtual bool accept(const char *u) = 0;
};

class all_filenames : public filename_filter {
public:
	all_filenames() { }
	virtual ~all_filenames() { }
	bool accept(const char *u) { return true; }
};

class fnmatch_filter : public filename_filter {
	const vector<string> &patterns;

public:
	fnmatch_filter(const vector<string> &Patterns) :
		patterns(Patterns)
	{
	}
	virtual ~fnmatch_filter() { }
	bool accept(const char *u) {
		for (auto &p: patterns) {
			if (fnmatch(p.c_str(), u, 0) == 0)
				return false;
		}
		return true;
	}
};

all_filenames all_filenames_singleton;

class file_comparator : non_copyable
{
	const vector<string> &names;
	typedef pair<unsigned, unsigned> couple;
	map<couple, int> results;
	tickable *tck;

	file_comparator();
	file_comparator(const file_comparator&);
	file_comparator &operator=(const file_comparator&);

public:
	file_comparator(const vector<string> &Names, tickable *Tck=NULL) :
		names(Names),
		tck(Tck)
	{
	}

	virtual ~file_comparator() { }

	int operator()(unsigned i, unsigned j) {
		assert (0 <= i && i < names.size());
		assert (0 <= j && j < names.size());
		if (i == j) return 0;
		if (i > j) return -operator()(j, i);
		auto it = results.find(couple(i, j));
		if (it == results.end()) {
			int r = file_utils::compare(
					names[i].c_str(),
					names[j].c_str(),
					tck);

			results[couple(i,j)] = r;
			return r;
		} else return it->second;
	}

	class proxy {
		file_comparator &target;

	public:
		proxy(file_comparator &Target) : target(Target) { }
		bool operator()(unsigned i, unsigned j) {
			int r = target(i, j);
			return r < 0;
		}
	};
};

class trie {
	bool terminal;
	typedef map<char, trie> dictionary;
	dictionary entries;

public:
	trie() : terminal(false) { }
	virtual ~trie() { }

	void add(const char *u) {
		char c = *u;

		if (!c) terminal = true;
		else entries[c].add(u + 1);
	}

	void dump(FILE *out, const string& u) {
		if (terminal) fprintf(out, "%s\n", u.c_str());
		for (dictionary::iterator it = entries.begin();
			it != entries.end();
			it ++) {
			string v = u;
			v.push_back(it->first);
			it->second.dump(out, v);
		}
	}

	void dump(FILE *out) {
		dump(out, string());
	}
};

class collector : non_copyable {
	typedef forward_list<file_info*> file_infos;
	typedef map< file_key, forward_list<file_info> > key_map;
	typedef map< file_id, file_infos > id_map;
	key_map key_collection;
	id_map id_collection;
	path current;
	progress pg;
	off_t min_size;
	const int hash_iterations;
	file_info dummy;
	off_t saveable_space;
	vector < pair< file_id, vector <file_info *> > > dupes;
	filename_filter &dir_filter;
	off_t ignored_dir_count, file_count, hard_link_count,
	      eligible_file_count,
	      duplicate_count;
	off_t eligible_byte_count;
	bool verbose;
	bool exact;
	mode_t chmod_clear;
	bool debug;
	string_pool sp;

	struct file_info_string : public lazy_string {
		const string_pool &sp;
		file_info *fi;
		string p;

		file_info_string(const string_pool &Sp) :
			sp(Sp), fi(NULL)
		{
		}

		const char *get() {
			if (fi == NULL) return "";
			p = fi->get_path(sp);
			return p.c_str();
		}
		void set(file_info *Fi) { fi = Fi; }
	};

	file_info_string fis;
	const talk &talker;

public:
	collector(
			off_t Min_size,
			int Hash_iterations,
			filename_filter& Dir_filter,
			bool Exact,
			mode_t Chmod_clear,
			bool Debug,
			bool Progress,
			const talk &Talker
		) :
			pg(stderr, fis, 0, Progress),
			min_size(Min_size),
			hash_iterations(Hash_iterations),
			saveable_space(0),
			dir_filter(Dir_filter),
			ignored_dir_count(0),
			file_count(0),
			hard_link_count(0),
			eligible_file_count(0),
			duplicate_count(0),
			eligible_byte_count(0),
			verbose(false),
			exact(Exact),
			chmod_clear(Chmod_clear),
			debug(Debug),
			fis(sp),
			talker(Talker)
	{
		dummy.clear();
	}

	virtual ~collector() {
	}

	void collect(const char *p) {
		current.set(p);
		collect(&dummy, p);
		string u = formatter::sprintf(
			"Files: %zu, eligibles: %zu, hard links: %zu.",
			file_count,
			eligible_file_count,
			hard_link_count);
		pg.finish(u.c_str());
	}

	void collect(const file_info *fip, const char *basename) {
		struct stat st;

		int rc = lstat(current.get().c_str(), &st);
		if (rc < 0) {
			talker.warning(
				"Warning: Cannot stat '%s': %s\n",
				current.get().c_str(),
				strerror(errno));
			pg.occupied();
		} else {
			file_count ++;

			bool is_dir = S_ISDIR(st.st_mode);
			bool is_eligible_file =
				S_ISREG(st.st_mode) && st.st_size >= min_size;

			if (!is_dir && !is_eligible_file) return;

			file_key fk(st.st_dev, st.st_ino);
			file_info fi;
			file_id fid;

			fi.name = sp.add(basename);
			fi.mode = st.st_mode;
			fi.ino = st.st_ino;
			fi.parent = fip;

			fid.dev = st.st_dev;
			fid.size = st.st_size;

			forward_list<file_info> &kv = key_collection[fk];
			bool has_known_links = !kv.empty();
			kv.push_front(fi);
			file_info &nfi = kv.front();

			if (has_known_links) {
				hard_link_count ++;
			} else if (is_dir) {
				if (dir_filter.accept(basename)) {
					try {
						collect_dir(&nfi);
					}
					catch(exception &e) {
						talker.warning("While "
							"collecting %s: %s",
							current.get().c_str(),
							e.what());
					}
				} else {
					ignored_dir_count ++;
					if (verbose) {
						talker.warning(
							"Ignoring %s",
							current.get().c_str());
						pg.occupied();
					}
				}
			} else if (is_eligible_file) {
				fis.set(&nfi);
				id_collection[fid].push_front(&nfi);
				pg.tick(1);
				eligible_file_count ++;
				eligible_byte_count += fid.size;
			}
		}
	}

	void collect_dir(const file_info *fip) {
		const struct dirent *e;
		string u;

		unix_dir d(current.get().c_str());

		while (d.read(e)) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
				continue;

			current.push(e->d_name);
			collect(fip, e->d_name);
			current.pop();
		}
	}

	template<class F>
	size_t generic_size(F &f) {
		size_t n = 0;
		for (auto __attribute__((unused)) &x: f) n ++;
		return n;
	}

	template<class F>
	void display_files(const char *msg, const file_id &fid,
			F &fiv)
	{
		fmt::pf("%s %zu %zu", msg,
				generic_size(fiv) * fid.size,
				fid.size);
		for (auto &fi: fiv) {
			fmt::pf(" ");
			fmt::print_quoted(stdout, fi->get_path(sp).c_str());
		}

		fmt::pf("\n");
	}

	template<class F>
	void display_files_debug(const char *msg, const file_id &fid,
			F &fiv)
	{
		if (debug) display_files(msg, fid, fiv);
	}

	void register_duplicates(const file_id &fid,
			vector<file_info*> &fiv)
	{
		dupes.push_back(pair<file_id, vector<file_info *> >(fid, fiv));

		size_t m = fiv.size();
		duplicate_count += m;
		saveable_space += (m - 1) * fid.size;
	}

	void register_collisions(uint64_t hash,
			const file_id &fid, file_infos &fiv)
	{
		string u = formatter::sprintf("collisions 0x%016"PRIx64,
				hash);
		display_files(u.c_str(), fid, fiv);
	}

	bool verify_equality(const file_id &fid, file_infos &fis)
	{
		display_files_debug("verify", fid, fis);

		if (!exact) return true;

		bool first = true;
		string p_0, p_i;

		for (auto &fi: fis) {
			if (first) {
				p_0 = fi->get_path(sp);
				first = false;
			} else {
				p_i = fi->get_path(sp);

				if (file_utils::compare(p_0.c_str(),
							p_i.c_str()))
					return false;
			}
		}

		return true;
	}

	typedef vector<vector<file_info *> > file_cong;

	file_cong congruence(const file_id &fid, vector<file_info *> &fiv)
	{
		const unsigned m = fiv.size();

		assert(fiv.size() > 1);

		vector<string> names(m);
		vector<unsigned> sigma(m);

		for(unsigned i = 0; i < m; i ++) {
			names[i] = fiv[i]->get_path(sp);
			sigma[i] = i;
		}

		file_comparator fc(names, &pg);

		file_cong cong;

		unsigned j;
		for (j = 1; j < m && fc(j, j - 1) == 0; j ++);
		if (j == m) {
			cong.resize(1);
			cong[0] = fiv;
			return cong;
		}

		file_comparator::proxy fcp(fc);
		sort(sigma.begin(), sigma.end(), fcp);

		unsigned c = 0;

		cong.resize(1);
		cong[c].push_back(fiv[sigma[0]]);

		for(unsigned i = 1; i < m; i ++) {
			if (fc(sigma[i], sigma[i - 1])) {
				c ++;
				cong.resize(c + 1);
			}
			cong[c].push_back(fiv[sigma[i]]);
		}

		return cong;
	}

	void equal_files(const file_id &fid, vector<file_info *> &fiv) {
		register_duplicates(fid, fiv);
	}

	void equal_files(const file_id &fid, const file_infos &fis) {
		vector<file_info *> fiv = file_infos_vectorize(fis);
		equal_files(fid, fiv);
	}

	vector<file_info *> file_infos_vectorize(const file_infos &fis) {
		size_t m = generic_size(fis);
		vector<file_info *> fiv(m);
		unsigned i = 0;
		for (auto& fi: fis) fiv[i ++] = fi;
		return fiv;
	}

	off_t get_saveable_space(void) {
		return saveable_space;
	}

	off_t get_ignored_dir_count(void) {
		return ignored_dir_count;
	}

	void check_bundle(uint64_t hash,
			const file_id &fid, file_infos &fis,
			int iterations)
	{
		size_t m = generic_size(fis);

		if (m == 1) return;
		assert(m > 1);
		display_files_debug("check_bundle", fid, fis);

		if (iterations == 0 || (exact && m == 2)) {
			if (verify_equality(fid, fis))
				equal_files(fid, fis);
			else if (m > 2)
				register_collisions(hash, fid, fis);
			return;
		}

		map<uint64_t, vector<file_info*> > resolve;
		checksummer c;

		if (m <= 2) {
			for (auto& fi: fis)
				resolve[0].push_back(fi);
		} else for (auto& fi: fis) {
			string u = fi->get_path(sp);
			try {
				uint64_t sum = c.checksum(u.c_str());
				if (debug)
					fmt::pf("csum 0x%016"PRIx64" '%s'\n",
						sum, u.c_str());
				resolve[sum].push_back(fi);
			} catch(...) {
				fmt::pf("csum (error) '%s'", u.c_str());
			}
		}

		for (auto &it: resolve) {
			if (it.second.size() <= 1) continue;
			file_cong cong = congruence(fid, it.second);

			for (auto &it2: cong) {
				if (it2.size() > 1)
					equal_files(fid, it2);
			}
		}
	}

	void check() {
		pg.reset(eligible_byte_count, 20);
		pg.occupied();

		for (auto &it: id_collection) {
			fis.set(it.second.front());
			check_bundle(0, it.first, it.second, hash_iterations);
		}
		fis.set(NULL);

		string u = formatter::sprintf(
				"Duplicate file count: %zu.", duplicate_count);
		pg.finish(u.c_str());
	}

	void hard_link(const file_id &fid, vector<file_info*> &fiv) {
		display_files_debug("hard_link", fid, fiv);

		string source = fiv[0]->get_path(sp);
		vector<string> targets;

		for (unsigned i = 1; i < fiv.size(); i ++) {
			file_key fk(fid.dev, fiv[i]->ino);
			for (auto &fi: key_collection[fk])
				targets.push_back(fi.get_path(sp));
		}

		file_utils::hard_link(source, targets, pg, talker);

		if (chmod_clear) {
			int rc = chmod(source.c_str(),
					fiv[0]->mode & ~chmod_clear);
			if (rc < 0) {
				talker.warning(
					"Warning: can't chmod "
					"'%s': %s",
					source.c_str(),
					strerror(errno));
				pg.occupied();
			}
		}
	}

	void hard_link_duplicates() {
		fmt::fpf(stderr, "Hard-linking duplicates.\n");
		pg.occupied();
		pg.reset();
		for (auto &d: dupes)
			hard_link(d.first, d.second);
	}

	void dump_duplicates() {
		for (auto &d: dupes)
			display_files("duplicates", d.first, d.second);
	}
};

class arguments {
	size_t i;
	vector<string> args;
	bool dry_run;
	const char *description;
	const char *progname;

public:
	class out_of_arguments : runtime_error {
		out_of_arguments() : runtime_error("Out of arguments") { }
	};

	arguments(int argc, const char * const * argv, const char *Description)
		:
		i(0),
		dry_run(false),
		description(Description) {
		progname = argv[0];
		args.resize(argc - 1);
		for (int j = 0; j < argc - 1; j ++)
			args[j] = argv[j + 1];
	}

	virtual ~arguments() { }

	bool processing() {
		return dry_run;
	}

	bool run(const char *msg) {
		if (dry_run) {
			fmt::fpf(stderr, " - %s\n", msg);
			return false;
		} else return true;
	}
	void dry() { i = 0; dry_run = true; }
	bool is_empty() { return i == args.size(); }
	string &front() { return args[i]; }

	bool pop_all(void) {
		i = args.size();
		return true;
	}

private:
	bool pop_int_fmt(int &o, const char *fmt) {
		if (dry_run) {
			fmt::fpf(stderr, " <integer>");
			return true;
		}
		if (is_empty()) return false;
		string u = front();
		int n;

		if (1 == sscanf(u.c_str(), fmt, &o, &n) &&
			u.c_str()[n] == 0) {
			pop();
			return true;
		} else return false;
	}

public:
	bool pop_int(int &o) {
		return pop_int_fmt(o, "%d%n");
	}

	bool pop_int_octal(int &o) {
		return pop_int_fmt(o, "%o%n");
	}

	bool pop_string_vector(const char *dsc, vector<string> &v) {
		if (dry_run) {
			fmt::fpf(stderr,
				" (<%s:string>|'' <%s:string> ... '')",
				dsc, dsc);
			return true;
		}

		string u = front(); pop();

		if (u.size() != 0) {
			v.push_back(u);
			return true;
		}

		while (!is_empty()) {
			string u = front(); pop();
			if (u.size() == 0)
				return true;
			v.push_back(u);
		}
		
		return true;
	}

	bool pop_empty_string() {
		if (dry_run) {
			fmt::fpf(stderr, " ''");
			return true;
		}

		if (!is_empty() && front().size() == 0) {
			pop();
			return true;
		} else return false;
	}

	bool pop_string(const char *dsc, string &u) {
		if (dry_run) {
			fmt::fpf(stderr, " <%s:string>", dsc);
			return true;
		}
		if (is_empty()) return false;
		u = front(); pop();
		return true;
	}

	bool pop_keyword(const char *u) {
		if (dry_run) {
			fmt::fpf(stderr, " %s", u);
			return true;
		}
		if (is_empty()) return false;
		return front().compare(u) == 0 && pop();
	}

	bool pop_keyword(const char *u1, const char *u2) {
		if (dry_run) {
			fmt::fpf(stderr, " (%s|%s)", u1, u2);
			return true;
		}
		if (is_empty()) return false;
		return (front().compare(u1) == 0 || front().compare(u2) == 0)
			&& pop();
	}

	bool pop() {
		if (i < args.size()) {
			i ++;
			return true;
		} else return false;
	}

	bool usage() {
		if (dry_run) {
			dry_run = false;
			return true;
		}

		fmt::fpf(stderr, "Usage: %s %s\nArguments:\n", progname,
				description);
		dry();
		pop_all();

		return true;
	}

	bool error()
	{
		if (dry_run) {
			dry_run = false;
			return true;
		}

		if (is_empty()) {
			fmt::fpf(stderr, "Error: missing argument\n");
		} else {
			fmt::fpf(stderr, "Error: Unknown argument "
					"#%zu/%zu '%s'\n",
					i,
					args.size(),
					args[i].c_str()
			);
		}
		return usage();
	}
};

struct options : public talk_control {
	string path;
	int min_size;
	bool hard_link;
	bool dump;
	bool exact;
	vector<string> ignored_dirs;
	vector<string> files;
	int chmod_clear;
	bool debug;
	bool progress;
	bool show_info;
	bool show_warnings;

	bool info_enabled() const { return show_info; }
	bool warnings_enabled() const { return show_warnings; }

	options() :
		min_size(100000),
		hard_link(false),
		dump(false),
		exact(true),
		chmod_clear(0222),
		debug(false),
		progress(true),
		show_info(true),
		show_warnings(true)
	{ }
};

void do_collect(const options &o, const char *progname)
{
	talk talker(o, progname);
	fnmatch_filter fm(o.ignored_dirs);
	collector c(o.min_size, 1, fm, o.exact, o.chmod_clear, o.debug,
			o.progress, talker);
	talker.info("Collecting '%s' (minimum size %zd)",
			o.path.c_str(), o.min_size);
	c.collect(o.path.c_str());
	talker.info("Checking");
	c.check();
	talker.info("Ignored dirs: %zd", c.get_ignored_dir_count());
	if (o.dump) {
		c.dump_duplicates();
	}
	talker.info("Bytes saveable: %zd", c.get_saveable_space());
	if (o.hard_link) {
		c.hard_link_duplicates();
	}
}

static const char *description =
	"[options] path\n"
	"\n"
	"Find files that have identical content and are on the same device.\n"
	"With --hard-link, make all copies a hard link to one of the files\n"
	"to save space."
	"\n"
	;

int main(int argc, const char * const * argv)
{
	int rc = 0;
	options o;
	string u;

	arguments args(argc, argv, description);

	do {
		(
		 	args.pop_keyword("-h", "--help") &&
			args.run("Print help") &&
			args.usage()
		) ||
		(
			args.pop_keyword("-v", "--version") &&
			args.run("Show version") &&
			(fmt::pf("fhlink compiled on " 
				__DATE__ " at " __TIME__ "\n"
				"Copyright (C)2012 Berke DURAK "
				"<berke.durak@gmail.com>\n"
				"Released under the GNU GPL v3.\n"),
			 true)
		) ||
		(
		 	args.pop_keyword("-m", "--min-size") &&
			args.pop_int(o.min_size) &&
			args.run("Ignore files smaller than this (bytes)")
		) ||
		(
		 	args.pop_keyword("-H", "--hard-link") &&
			args.run("De-duplicate files by creating hard links") &&
			(o.hard_link = true, true)
		) ||
		(
		 	args.pop_keyword("-d", "--dump") &&
			args.run("Dump duplicates") &&
			(o.dump = true, true)
		) ||
		(
		 	args.pop_keyword("-i", "--ignore-dirs") &&
			args.pop_string_vector("pattern", o.ignored_dirs) &&
			args.run("Ignore directories matching given patterns")
		) ||
		(
		 	args.pop_keyword("-a", "--approximate") &&
			args.run("Do not perform exact file comparison") &&
			(o.exact = false, true)
		) ||
		(
		 	args.pop_keyword("-c", "--chmod-clear") &&
			args.pop_int_octal(o.chmod_clear) &&
			args.run("Clear mode bits from deduplicated files "
				"(0222 by default)")
		) ||
		(
		 	args.pop_keyword("-W", "-no-warnings") &&
			args.run("Disable warning messages") &&
			(o.show_warnings = false, true)
		) ||
		(
		 	args.pop_keyword("-I", "--no-information") &&
			args.run("Disable informational messages") &&
			(o.show_info = false, true)
		) ||
		(
		 	args.pop_keyword("-P", "--no-progress") &&
			args.run("Don't show a progress indicator") &&
			(o.progress = false, true)
		) ||
		(
		 	args.pop_keyword("--debug") &&
			args.run("Enable debuging") &&
			(o.debug = true, true)
		) ||
		(
			args.pop_string("path", o.path) &&
			args.run("Scan files under <path>") &&
			args.is_empty() &&
			(do_collect(o, argv[0]), true)
		) ||
		args.error();
	} while (!args.is_empty() || args.processing());

	return rc;
}

// vim:set sw=8 ts=8 noexpandtab:
