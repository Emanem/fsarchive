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
#include <zip.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string.h>

#include <iostream>

namespace {
	const char						*FS_ARCHIVE_MAIN = "fsarchive_main.zip",
	      							*FS_ARCHIVE_DELTA = "fsarchive_delta_";
	
	const zip_uint16_t					FS_ZIP_EXTRA_FIELD_ID = 0xe0e0;
	
	const uint32_t						FS_TYPE_FILE_NEW = 0,
	      							FS_TYPE_FILE_MOD = 1,
								FS_TYPE_FILE_DEL = 2;

	typedef struct fsarc_stat64 {
		mode_t fs_mode;
		uid_t fs_uid;
		gid_t fs_gid;
		uint32_t fs_type;
		time_t fs_atime;
		time_t fs_mtime;
		time_t fs_ctime;
		off64_t fs_size;
		char	fs_prev[32];
	} fsarc_stat64_t;

	static_assert(sizeof(fsarc_stat64_t) == (48 + 32), "sizeof(fsarc_stat64_t) is not 48 + 32 bytes");

	typedef std::unordered_map<std::string, fsarc_stat64_t>	fileset_t;

	// utility to combine paths and cater for final /
	// both need to be longer than 0
	std::string combine_paths(const std::string& a, const std::string& b) {
		if('/' == *(a.rbegin()))
			return a + b;
		return a + '/' + b;
	}

	// check that a path is a valid directory
	// and reports if contains fsarchive_main or not
	bool check_dir_fsarchive_main(const std::string& p, std::string& ar_main_path, std::string& ar_delta_path) {
		struct stat64 s = {0};
		if(lstat64(p.c_str(), &s))
			throw fsarchive::rt_error("Invalid/unable to lstat64 directory: ") << p;
		if(!S_ISDIR(s.st_mode))
			throw fsarchive::rt_error("Not a directory: ") << p;
		// set main file path
		ar_main_path = combine_paths(p, FS_ARCHIVE_MAIN);
		ar_delta_path = combine_paths(p, FS_ARCHIVE_DELTA) + std::to_string(time(0)) + ".zip";
		// check for the main file
		if(lstat64(ar_main_path.c_str(), &s) ||
			!S_ISREG(s.st_mode))
			return false;
		return true;
	}

	fsarc_stat64_t from_stat64(const struct stat64& s, const char* prev = 0, const uint32_t type = FS_TYPE_FILE_NEW) {
		fsarc_stat64_t	fs_t = {0};
		fs_t.fs_mode = s.st_mode;
		fs_t.fs_uid = s.st_uid;
		fs_t.fs_gid = s.st_gid;
		fs_t.fs_type = type;
		fs_t.fs_atime = s.st_atime;
		fs_t.fs_mtime = s.st_mtime;
		fs_t.fs_ctime = s.st_ctime;
		fs_t.fs_size = s.st_size;
		if(prev) {
			memcpy(fs_t.fs_prev, prev, 32);
			fs_t.fs_prev[31] = '\0';
		} else {
			fs_t.fs_prev[0] = '\0';
		}
		return fs_t;
	}

	class zip_f {
		zip_t		*z_;
		fileset_t	f_map_;

		zip_f(const zip_f&);
		zip_f& operator=(const zip_f&);
	public:
		zip_f(const char* fname) : z_(zip_open(fname, ZIP_CREATE, 0)) {
			if(!z_)
				throw fsarchive::rt_error("Can't open/create zip archive ") << fname;
			// populate the entries
			const zip_int64_t	n_entries = zip_get_num_entries(z_, 0);
			for(zip_int64_t i = 0; i < n_entries; ++i) {
				zip_stat_t	st = {0};
				if(-1 == zip_stat_index(z_, i, 0, &st)) {
					zip_close(z_);
					throw fsarchive::rt_error("Can't stat file index ") << i;
				}
				zip_uint16_t	len = 0;
				const auto *pf = zip_file_extra_field_get_by_id(z_, i, FS_ZIP_EXTRA_FIELD_ID, 0, &len, ZIP_FL_LOCAL);
				if(pf) {
					f_map_[st.name] = *(fsarc_stat64_t*)pf;
				} else {
					zip_close(z_);
					throw fsarchive::rt_error("Couldn't find FS_ZIP_EXTRA_FIELD_ID for file ") << st.name;
				}
			}
		}

		bool add_file(const char* f, const char* prev = 0) {
			// if the file is already added to the archive
			// skip it
			if(f_map_.find(f) != f_map_.end())
				return false;
			std::unique_ptr<zip_source_t, void (*)(zip_source_t*)>	p_zf(
				zip_source_file_create(f, 0, -1, 0),
				[](zip_source_t* p) { if(p) zip_source_close(p); }
			);
			if(!p_zf)
				throw fsarchive::rt_error("Can't open source file for zip ") << f;
			const zip_int64_t idx = zip_file_add(z_, f, p_zf.get(), ZIP_FL_ENC_GUESS);
			if(-1 == idx)
				throw fsarchive::rt_error("Can't add file ") << f << " to the archive";
			// https://libzip.org/documentation/zip_file_extra_field_set.html
			// we can't use the info libzip stamps because the mtime is off
			// by one usecond, plus we need to store additional metadata
			struct stat64 s = {0};
			if(lstat64(f, &s))
				throw fsarchive::rt_error("Invalid/unable to lstat64 file: ") << f;
			const auto fs_t = from_stat64(s, prev);
			if(zip_file_extra_field_set(z_, idx, FS_ZIP_EXTRA_FIELD_ID, 0, (const zip_uint8_t*)&fs_t, sizeof(fs_t), ZIP_FL_LOCAL))
				throw fsarchive::rt_error("Can't set extra field FS_ZIP_EXTRA_FIELD_ID for file ") << f;
			f_map_[f] = fs_t;
			return true;
		}

		const fsarc_stat64_t& get_stat_file(const char *f) {
			const auto it = f_map_.find(f);
			if(f_map_.end() == it)
				throw fsarchive::rt_error("Can't find file ") << f << " in archive";
			return it->second;
		}

		~zip_f() {
			zip_close(z_);
		}
	};

	void r_archive_dir(const std::string& f, zip_f& z, fileset_t& existing_files) {
		struct stat64 s = {0};
		if(lstat64(f.c_str(), &s))
			throw fsarchive::rt_error("Invalid/unable to lstat64 file/directory: ") << f;
		if(!S_ISDIR(s.st_mode)) {
			if(S_ISREG(s.st_mode)) {
				if(!z.add_file(f.c_str()))
					existing_files[f] = from_stat64(s);
			}
		} else {
			std::unique_ptr<DIR, void (*)(DIR*)> p_dir(opendir(f.c_str()), [](DIR *d){ if(d) closedir(d);});
			struct dirent64	*de = 0;
			while((de = readdir64(p_dir.get()))) {
				if(std::string(".") == de->d_name ||
				   std::string("..") == de->d_name)
					continue;
				if(DT_REG == de->d_type || DT_DIR == de->d_type)
					r_archive_dir(combine_paths(f, de->d_name), z, existing_files);
			}
		}
	}
}

void fsarchive::init_update_archive(char *in_dirs[], const int n) {
	using namespace fsarchive;

	// let's check that we have a valid directory
	std::string	ar_main_path,
			ar_delta_path;
	const bool 	has_main = check_dir_fsarchive_main(settings::AR_DIR, ar_main_path, ar_delta_path);
	// no matter what generate or open a FS_ARCHIVE_MAIN
	// file
	zip_f		z(ar_main_path.c_str());
	fileset_t	existing_files;
	for(int i=0; i < n; ++i)
		r_archive_dir(in_dirs[i], z, existing_files);
	if(!has_main) {
		// it means we have generated a new file
		for(const auto& kv : existing_files)
			std::cout << "Warning: file " << kv.first << " was listed twice";
		return;
	} else {
		// we need to generate a new 'delta' archive
		zip_f		z_delta(ar_delta_path.c_str());
		for(const auto& f : existing_files) {
			const auto& zs = z.get_stat_file(f.first.c_str());
			if(f.second.fs_mtime > zs.fs_mtime || f.second.fs_size != zs.fs_size) {
				std::cout << "File " << f.first << " has changed: " << f.second.fs_mtime << "\t" << zs.fs_mtime << std::endl;
				if(!z_delta.add_file(f.first.c_str()))
					throw fsarchive::rt_error("Can't add file ") << f.first << " to delta archive " << ar_delta_path;
			}
		}
	}
}
