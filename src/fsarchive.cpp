/*
*	fsarchive (C) 2023 E. Oriani, ema <AT> fastwebnet <DOT> it
*
*	This file is part of fsarchive.
*
*	fsarchive is free software: you can redistribute it and/or modify
*	it under the terms of the GNU General Public License as published by
*	the Free Software Foundation, either version 3 of the License, or
*	(at your option) any later version.
*
*	fsarchive is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*	GNU General Public License for more details.
*
*	You should have received a copy of the GNU General Public License
*	along with fsarchive.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "fsarchive.h"
#include "settings.h"
#include "utils.h"
#include "log.h"
#include "zip_fs.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <utime.h>
#include <string.h>
#include <memory>
#include <sstream>
#include <fstream>
#include <regex>

extern "C" {
#include "bsdiff.h"
#include "bspatch.h"
}

namespace {
	using namespace fsarchive;

	typedef struct bspatch_stream				bspatch_stream_t;

	typedef struct bsdiff_stream				bsdiff_stream_t;

	struct bspatch_s {
		size_t		idx;
		const buffer_t&	data;

		bspatch_s(const buffer_t& d) : idx(0), data(d) {
		}
	};

	typedef std::vector<std::regex>				regexvec_t;

	typedef std::unique_ptr<zip_fs>				pzip_fs_t;

	typedef std::unique_ptr<const zip_fs>			cpzip_fs_t;

	typedef std::unordered_map<std::string, cpzip_fs_t>	zipfscache_t;

	typedef std::unique_ptr<log::progress>			pprogress_t;

	typedef struct {
		regexvec_t	r_excl;
		int64_t		sz_excl;
	} excl_t;

	extern "C" {
		int fsarc_bspatch_read(const struct bspatch_stream* stream, void* buffer, int length) {
			bspatch_s*	bs_s = (bspatch_s*)stream->opaque;
			if((bs_s->idx + length) <= bs_s->data.size()) {
				memcpy(buffer, bs_s->data.data() + bs_s->idx, length);
				bs_s->idx += length;
				return 0;
			}
			return -1;
		}

		int fsarc_bsdiff_write(struct bsdiff_stream* stream, const void* buffer, int size) {
			std::stringstream	*s = (std::stringstream*)stream->opaque;
			s->write((const char*)buffer, size);
			return 0;
		}
	}

	// utility to combine paths and cater for final /
	// both need to be longer than 0
	std::string combine_paths(const std::string& a, const std::string& b) {
		if('/' == *(a.rbegin()))
			return a + b;
		return (!a.empty()) ? a + '/' + b : b;
	}

	void init_paths(const std::string& s, mode_t mode = 0755) {
		if(settings::DRY_RUN)
			return;

		size_t		pos=0;
		std::string	dir;

		if(s[s.size()-1]!='/')
			throw fsarchive::rt_error("Have to pass a path ending with '/' ") << dir;

		while((pos=s.find_first_of('/',pos))!=std::string::npos){
			dir=s.substr(0,pos++);
			if(dir.size()==0) continue; // if leading / first time is 0 length
			if(mkdir(dir.c_str(),mode) && errno!=EEXIST)
				throw fsarchive::rt_error("Can't init path ") << dir;
		}
	}

	void write_file(const std::string& f, const buffer_t& buf_file) {
		if(settings::DRY_RUN)
			return;

		std::ofstream	ostr(f, std::ios_base::binary);
		if((long int)buf_file.size() != ostr.write((const char*)buf_file.data(), buf_file.size()).tellp())
			throw fsarchive::rt_error("Can't restore file ") << f << " on the disk";
	}

	void update_metadata(const std::string& f, const stat64_t& s) {
		if(settings::DRY_RUN)
			return;

		if(chmod(f.c_str(), 07777 & s.fs_mode))
			LOG_WARNING << "Can't set permissions for file/directory " << f;
		// the below cast should work because the structure is aligned
		// with the spec of utime data structure https://linux.die.net/man/2/utime
		if(utime(f.c_str(), (const struct utimbuf *)&s.fs_atime))
			LOG_WARNING << "Can't update times for file/directory " << f;
		if(chown(f.c_str(), s.fs_uid, s.fs_gid))
			LOG_WARNING << "Can't set user/group id for file/directory " << f;
	}

	void load_file(const std::string& f, buffer_t& out) {
		out.clear();
		std::ifstream	istr(f, std::ios_base::binary);
		const auto sz = istr.seekg(0, std::ios_base::end).tellg();
		out.resize(sz);
		if(istr.seekg(0, std::ios_base::beg).read((char*)out.data(), sz).tellg() != sz)
			throw fsarchive::rt_error("Can't read binary file ") << f;
	}

	// check that a path is a valid directory
	// and reports if contains fsarchive_main or not
	void check_dir_fsarchives(const std::string& p, std::string& ar_next_path, filelist_t& ar_files) {
		struct stat64 s = {0};
		if(lstat64(p.c_str(), &s))
			throw fsarchive::rt_error("Invalid/unable to lstat64 directory: ") << p;
		if(!S_ISDIR(s.st_mode))
			throw fsarchive::rt_error("Not a directory: ") << p;
		// set next file path
		char	ts[68] = {0};
		{
			struct tm	cur_tm = {0};
			const time_t	cur_t = time(0);
			localtime_r(&cur_t, &cur_tm);
			snprintf(ts, sizeof(ts), "%04d%02d%02d-%02d%02d%02d", 1900+cur_tm.tm_year, 1+cur_tm.tm_mon, cur_tm.tm_mday, cur_tm.tm_hour, cur_tm.tm_min, cur_tm.tm_sec);
		}
		ar_next_path = combine_paths(p, FS_ARCHIVE_BASE) + ts + ".zip";
		// then open current directory and scen for fsarchive zip files
		ar_files.clear();
		std::unique_ptr<DIR, void (*)(DIR*)> p_dir(opendir(p.c_str()), [](DIR *d){ if(d) closedir(d);});
		struct dirent64	*de = 0;
		while((de = readdir64(p_dir.get()))) {
			if(std::string(".") == de->d_name ||
			   std::string("..") == de->d_name)
				continue;
			if(DT_REG == de->d_type) {
				if(strstr(de->d_name, FS_ARCHIVE_BASE) == de->d_name)
					ar_files.insert(de->d_name);
			}
		}
	}

	stat64_t fsarc_stat64_from_stat64(const struct stat64& s, const char* prev = 0, const uint32_t type = FS_TYPE_FILE_NEW) {
		stat64_t	fs_t = {0};
		fs_t.fs_mode = s.st_mode;
		fs_t.fs_uid = s.st_uid;
		fs_t.fs_gid = s.st_gid;
		fs_t.fs_type = type;
		fs_t.fs_atime = s.st_atime;
		fs_t.fs_mtime = s.st_mtime;
		fs_t.fs_ctime = s.st_ctime;
		fs_t.fs_size = s.st_size;
		if(prev) {
			strncpy(fs_t.fs_prev, prev, 31);
			fs_t.fs_prev[31] = '\0';
		} else {
			fs_t.fs_prev[0] = '\0';
		}
		return fs_t;
	}

	template<typename fn_on_elem>
	void r_fs_scan(const std::string& f, fn_on_elem&& on_elem, const excl_t& excls) {
		// first check we are not a match anywhere in our
		// exclusions regex
		for(const auto& r : excls.r_excl) {
			std::smatch	s;
			if(std::regex_match(f, s, r)) {
				LOG_INFO << "File " << f << " is excluded";
				return;
			}
		}
		struct stat64 s = {0};
		if(lstat64(f.c_str(), &s))
			throw fsarchive::rt_error("Invalid/unable to lstat64 file/directory: ") << f;
		// exclusions for size
		if((excls.sz_excl > 0) && (s.st_size > excls.sz_excl)) {
			LOG_INFO << "File " << f << " is size excluded";
			return;
		}
		if(!S_ISDIR(s.st_mode)) {
			if(S_ISREG(s.st_mode)) {
				on_elem(f, s);
			}
		} else {
			on_elem(f, s);
			std::unique_ptr<DIR, void (*)(DIR*)> p_dir(opendir(f.c_str()), [](DIR *d){ if(d) closedir(d);});
			// this is the case when we try to opena  directory we don't have permissions on
			if(!p_dir)
				throw fsarchive::rt_error("Invalid/unable to opendir directory: ") << f;
			struct dirent64	*de = 0;
			while((de = readdir64(p_dir.get()))) {
				if(std::string(".") == de->d_name ||
				   std::string("..") == de->d_name)
					continue;
				if(DT_REG == de->d_type || DT_DIR == de->d_type)
					r_fs_scan(combine_paths(f, de->d_name), on_elem, excls);
			}
		}
	}

	void r_rebuild_file(const zip_fs& c_fs, const std::string& f, buffer_t& data, zipfscache_t& zcache) {
		using namespace fsarchive;

		auto fn_get_fromcache = [&zcache](const std::string& f) -> const zip_fs& {
			const auto it_c = zcache.find(f);
			if(zcache.end() != it_c)
				return *it_c->second;
			const auto rv = zcache.insert(zipfscache_t::value_type(f, std::make_unique<zip_fs>(f, true)));
			if(!rv.second)
				throw fsarchive::rt_error("Internal zip_fs cache corrupted, double entry for ") << f;
			return *rv.first->second;
		};

		stat64_t	s = {0};
		if(!c_fs.extract_file(f, data, s))
			throw fsarchive::rt_error("Can't extract file ") << f << " from archive (file not present)";
		// then see if the file is full or not or unchanged
		if(FS_TYPE_FILE_NEW == s.fs_type) {
			// if the file is new, nothing to do
			LOG_INFO << "File '" << f << "' has been rebuilt as is (NEW)";
			return;
		} else if(FS_TYPE_FILE_UNC == s.fs_type) {
			// if ile is unchanged, fetch it from the correct
			// prev entry
			const zip_fs&	p_fs = fn_get_fromcache(combine_paths(settings::AR_DIR, s.fs_prev));
			r_rebuild_file(p_fs, f, data, zcache);
			LOG_INFO << "File '" << f << "' has been forwarded as is (UNC) from " << s.fs_prev;
			return;
		} else if(FS_TYPE_FILE_MOD == s.fs_type) {
			// if the file is modified, we first need to
			// - get the original
			buffer_t	p_data;
			const zip_fs&	p_fs = fn_get_fromcache(combine_paths(settings::AR_DIR, s.fs_prev));
			r_rebuild_file(p_fs, f, p_data, zcache);
			// - apply current patch
			// s will contain the full size of the patched file
			buffer_t	n_data(s.fs_size);
			bspatch_s	bs_s(data);
			bspatch_stream_t bsp_s = {
				.opaque = (void*)&bs_s,
				.read = fsarc_bspatch_read,
			};
			// finally patch it
			if(bspatch(p_data.data(), p_data.size(), n_data.data(), n_data.size(), &bsp_s))
				throw fsarchive::rt_error("Couldn't patch file ") << f << " from archive";
			data.swap(n_data);
			LOG_INFO << "File '" << f << "' has been patched (MOD) from " << s.fs_prev;
			return;
		}
		throw fsarchive::rt_error("Invalid metadata fs_type ") << s.fs_type;
	}

	regexvec_t init_regex(const fsarchive::settings::excllist_t& f) {
		// used to sanitize input
		const static std::regex	special_chars { R"([-[\]{}()*+?.,\^$|#\s])" };
		regexvec_t	rv;
		for(const auto& r : f) {
			std::stringstream	cur_regex;
			// find all occurences of '*'
			const char	*p_cur = r.c_str(),
					*p_next_c = 0;
			while((p_next_c = strpbrk(p_cur, "*?"))) {
				cur_regex << std::regex_replace(std::string(p_cur, p_next_c), special_chars, R"(\$&)");
				switch(*p_next_c) {
					case '*':
						cur_regex << ".*";
						break;
					case '?':
						cur_regex << "[^/]+";
						break;
					default:
						throw fsarchive::rt_error("Invalid break RegEx sequence: ") << *p_next_c;
				}
				p_cur = p_next_c+1;
			}
			cur_regex << std::regex_replace(std::string(p_cur), special_chars, R"(\$&)");
			//
			rv.push_back(std::regex(cur_regex.str()));
		}

		return rv;
	}
}

void fsarchive::init_update_archive(char *in_dirs[], const int n) {
	using namespace fsarchive;

	// init exclusions regex
	excl_t	excl = {
		.r_excl = init_regex(settings::AR_EXCLUSIONS),
		.sz_excl = settings::AR_SZ_FILTER,
	};
	const regexvec_t	ar_comp_filter = init_regex(settings::AR_COMP_FILTER);
	auto 			fn_comp_filter	= [&ar_comp_filter](const std::string& f) -> bool {
		for(const auto& r : ar_comp_filter) {
			std::smatch	s;
			if(std::regex_match(f, s, r)) {
				LOG_INFO << "File " << f << " won't be compressed";
				return true;
			}
		}
		return false;
	};
	// let's check that we have a valid directory
	std::string		ar_next_path;
	filelist_t		ar_files;
	check_dir_fsarchives(settings::AR_DIR, ar_next_path, ar_files);
	// if we don't have any files or the AR_FORCE_NEW is set
	// then write from scratch
	if(ar_files.empty() || settings::AR_FORCE_NEW) {
		LOG_INFO << "Building an archive from scratch: " << ar_next_path;
		pzip_fs_t	z(settings::DRY_RUN ? 0 : std::make_unique<zip_fs>(ar_next_path.c_str(), false));
		auto fn_on_elem = [&z, &fn_comp_filter](const std::string& f, const struct stat64& s) -> void {
			if(S_ISREG(s.st_mode)) {
				if(z)
					z->add_file_new(f, fsarc_stat64_from_stat64(s), fn_comp_filter(f));
				LOG_INFO << "File '" << f << "' has been added as new (NEW)";
			} else if (S_ISDIR(s.st_mode)) {
				if(z)
					z->add_directory(f, fsarc_stat64_from_stat64(s));
				LOG_INFO << "Directory '" << f << "' has been added";
			}
		};
		for(int i=0; i < n; ++i)
			r_fs_scan(in_dirs[i], fn_on_elem, excl);
	} else {
		// otherwise load the latest archive
		LOG_INFO << "Building a delta archive: " << ar_next_path << " -> " << *ar_files.rbegin();
		const auto&	z_latest_name = *ar_files.rbegin();
		const zip_fs	z_latest(combine_paths(settings::AR_DIR, z_latest_name), true);
		// we need to generate a new 'delta' archive
		pzip_fs_t	z_next(settings::DRY_RUN ? 0 : std::make_unique<zip_fs>(ar_next_path.c_str(), false));
		// then we need to get all the files
		fileset_t	all_files;
		{
			auto fn_fileadd = [&all_files](const std::string& f, const struct stat64& s) -> void {
				if(S_ISREG(s.st_mode) || S_ISDIR(s.st_mode))
					all_files[f] = fsarc_stat64_from_stat64(s);
			};
			for(int i=0; i < n; ++i)
				r_fs_scan(in_dirs[i], fn_fileadd, excl);
		}
		// then we should have 3 logical 'sets'
		// * new files
		// * mod(ified) files
		// * unc(changed) files
		// and we should manage accordingly
		fsarchive::log::progress	p_delta("Delta zip creation");
		size_t				p_num = 0;
		const auto&			latest_fileset = z_latest.get_fileset();
		zipfscache_t			zcache;
		for(const auto& f : all_files) {
			p_delta.update_completion(1.0*(p_num++)/all_files.size());
			// if f is a directory, just add it to the new archive
			if(S_ISDIR(f.second.fs_mode)) {
				if(z_next)
					z_next->add_directory(f.first, f.second);
				LOG_INFO << "Directory '" << f.first << "' has been added";
				continue;
			}
			// otherwise carry on...
			const auto	it_latest = latest_fileset.find(f.first);
			if(it_latest == latest_fileset.end()) {
				// brand new file
				if(z_next)
					z_next->add_file_new(f.first, f.second, fn_comp_filter(f.first));
				LOG_INFO << "File '" << f.first << "' has been added as new (NEW)";
			} else if((f.second.fs_mtime != it_latest->second.fs_mtime) ||
				  (f.second.fs_size != it_latest->second.fs_size)) {
				// in case we don't want any bsdiff
				// or current file is marked to be comp excluded
				const bool	is_comp_excl = fn_comp_filter(f.first);
				if(settings::AR_NO_BSDIFF || is_comp_excl) {
					if(z_next)
						z_next->add_file_new(f.first, f.second, is_comp_excl);
					LOG_INFO << "File '" << f.first << "' has been added as new (NEW - no bsdiff)";
					continue;
				}
				// changed file
				// first rebuild the file
				buffer_t	p_data;
				r_rebuild_file(z_latest, f.first, p_data, zcache);
				// create a bsdiff patch
				std::stringstream	s_diff;
				bsdiff_stream_t	bsd_s = {
					.opaque = (void*)&s_diff,
					.malloc = malloc,
					.free = free,
					.write = fsarc_bsdiff_write,
				};
				buffer_t	n_data;
				load_file(f.first, n_data);
				if(bsdiff(p_data.data(), p_data.size(), n_data.data(), n_data.size(), &bsd_s))
					throw fsarchive::rt_error("Couldn't diff file ") << f.first << " from archive";
				// and finally add it
				if(z_next)
					z_next->add_file_bsdiff(f.first, f.second, s_diff.str(), z_latest_name.c_str());
				LOG_INFO << "File '" << f.first << "' has been added as changed (MOD) -> " << z_latest_name;
			} else {
				// unchanged file
				// optimization - if the file is unchanged in z_latest as well,
				// then use its prev!
				const char *prev_unc = (FS_TYPE_FILE_UNC == it_latest->second.fs_type) ? it_latest->second.fs_prev : z_latest_name.c_str();
				if(z_next)
					z_next->add_file_unchanged(f.first, f.second, prev_unc);
				LOG_INFO << "File '" << f.first << "' has been added as unchanged (UNC) -> " << prev_unc;
			}
		}
		p_delta.update_completion(1.0);
	}
}

void fsarchive::restore_archive(void) {
	using namespace fsarchive;

	struct stat64 s = {0};
	if(settings::RE_FILE.empty() || lstat64(settings::RE_FILE.c_str(), &s) || !S_ISREG(s.st_mode))
		throw fsarchive::rt_error("Archive to restore is empty and/or file doesn't exist/is not accessible ") << settings::RE_FILE;

	const zip_fs	z(settings::RE_FILE, true);

	pprogress_t	p_restore(std::make_unique<log::progress>("Restoring zip data"));
	size_t		p_num = 0;
	const auto&	re_fs = z.get_fileset();
	auto fn_out_file = [](const std::string& f) -> std::string {
		if(f[0] == '/') {
			if(!settings::RE_DIR.empty())
				return combine_paths(settings::RE_DIR, &f[1]);
			else
				return f;
		} else {
			if(!settings::RE_DIR.empty())
				return combine_paths(settings::RE_DIR, f);
			else
				return f;
		}
	};
	zipfscache_t	zcache;
	for(const auto& f : re_fs) {
		p_restore->update_completion(1.0*(p_num++)/re_fs.size());
		const std::string	out_file = fn_out_file(f.first);
		// if the current file is a directory, add it and carry on
		if(S_ISDIR(f.second.fs_mode)) {
			init_paths(out_file);
			LOG_INFO << "Directory '" << out_file << "' restored";
			continue;
		}
		const auto		it_l_slash = out_file.find_last_of('/');
		if(it_l_slash != std::string::npos)
			init_paths(out_file.substr(0, it_l_slash+1));
		buffer_t	buf_file;
		r_rebuild_file(z, f.first, buf_file, zcache);
		write_file(out_file, buf_file);
	}
	p_restore->update_completion(1.0);
	p_restore.reset();
	// update metadata if so - default
	if(settings::RE_METADATA) {
		p_restore = std::make_unique<log::progress>("Restoring metadata");
		p_num = 0;
		// then change all permissions/ownership/etc etc
		for(const auto& f : re_fs) {
			p_restore->update_completion(1.0*(p_num++)/re_fs.size());
			const std::string	out_file = fn_out_file(f.first);
			update_metadata(out_file, f.second);
		}
		p_restore->update_completion(1.0);
	}
}

