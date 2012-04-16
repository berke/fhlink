// dupfinder 0.01
//
// Copyright (C)2012 Berke DURAK
// Released under the GPL3 license

#include <vector>
#include <string>
#include <map>
#include <stdexcept>

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

using namespace std;

struct file_key {
	dev_t dev;
	ino_t ino;
};

struct file_id {
	off_t size;
};

struct file_info {
	string name;
	mode_t mode;
	off_t size;
	struct file_info *parent;
};

class unix_rc {
public:
	unix_rc(int Rc) {
		check(Rc);
	}

	virtual ~unix_rc() { }

	int operator=(int Rc) {
		check(Rc);
		return Rc;
	}

	static void check(int Rc) {
		if (Rc < 0)
			throw runtime_error(
				string("Unix error: ") + strerror(errno));
	}
};

class unix_dir {
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
		u += '/';
		u += v;
	}

	void pop() {
		size_t pos = u.find_last_of('/');
		if (pos == 0 || pos == string::npos)
			throw runtime_error("Path empty");
		u.resize(pos);
	}

};

class collector {
	typedef map< file_key, file_info > key_map;
	typedef map< file_id, key_map::iterator > id_map;
	key_map key_collection;
	id_map id_collection;
	path current;

public:
	collector() { }

	virtual ~collector() {
	}

	void collect(const char *p) {
		current.set(p);
		collect();
	}

	void collect() {
		const struct dirent *e;
		struct stat st;
		string u;

		unix_dir d(current.get().c_str());

		while (d.read(e)) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
				continue;

			current.push(e->d_name);
			int rc = lstat(current.get().c_str(), &st);
			if (rc < 0) {
				fprintf(stderr,
					"Warning: Cannot stat '%s': %s\n",
					current.get().c_str(),
					strerror(errno));
			} else {
				if (S_ISDIR(st.st_mode))
					try {
						collect();
					}
					catch(...) {
					}
			}
			current.pop();
		}

		unix_rc rc2 = chdir("..");
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

		return string(u, n);
	}
};

class arguments {
	size_t i;
	vector<string> args;
	bool dry_run;

public:
	class out_of_arguments : runtime_error {
		out_of_arguments() : runtime_error("Out of arguments") { }
	};

	arguments(int argc, const char * const * argv) : i(0), dry_run(false) {
		args.resize(argc);
		for (int j = 0; j < argc; j ++)
			args[j] = argv[j];
	}

	virtual ~arguments() { }

	bool processing() {
		return dry_run;
	}

	bool run(const char *msg) {
		if (dry_run) {
			printf(" - %s\n", msg);
			return false;
		} else return true;
	}
	void dry() { i = 0; dry_run = true; }
	bool is_empty() { return i == args.size(); }
	string &front() { return args[i]; }

	bool pop_string(const char *dsc, string& u) {
		if (dry_run) {
			printf(" <%s:string>", dsc);
			return true;
		}
		if (is_empty()) return false;
		u = front(); pop();
		return true;
	}

	bool pop_keyword(const char *u) {
		if (dry_run) {
			printf(" %s", u);
			return true;
		}
		if (is_empty()) return false;
		return front().compare(u) == 0 && pop();
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

		printf("Arguments:\n");
		dry();

		return true;
	}

	bool error()
	{
		if (dry_run) {
			dry_run = false;
			return true;
		}

		if (is_empty()) {
			printf("Error: missing argument\n");
		} else {
			printf("Error: Unknown argument #%zu/%zu '%s'",
					i,
					args.size(),
					args[i].c_str()
			);
		}
		return usage();
	}
};

void do_collect(const string &p)
{
	collector c;
	printf("Collecting '%s'\n", p.c_str());
	c.collect(p.c_str());
}

int main(int argc, const char * const * argv)
{
	int rc = 0;
	string path;

	arguments args(argc - 1, argv + 1);

	do {
		(
		 	args.pop_keyword("--help") &&
			args.run("Print help") &&
			args.usage()
		) ||
		(
			args.pop_keyword("--version") &&
			args.run("Show version") &&
			(printf("dupfinder compiled on " 
				__DATE__ " at " __TIME__ "\n"),
			 true)
		) ||
		(
		 	args.pop_keyword("--scan") &&
			args.pop_string("path", path) &&
			args.run("Scan files under <path>") &&
			(do_collect(path),
			 true)
		) ||
		args.error();
	} while(args.processing());

	return rc;
}

// vim:set sw=8 ts=8 noexpandtab:
