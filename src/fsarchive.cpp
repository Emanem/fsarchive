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
#include <zip.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <memory>
#include <unordered_map>
#include <set>
#include <vector>
#include <sstream>
#include <fstream>
#include <string.h>

extern "C" {
#include "bsdiff.h"
#include "bspatch.h"
}

namespace {
	const char						*FS_ARCHIVE_BASE = "fsarchive_";
	
	const zip_uint16_t					FS_ZIP_EXTRA_FIELD_ID = 0xe0e0;
	
	const uint32_t						FS_TYPE_FILE_NEW = 1,
								FS_TYPE_FILE_MOD = 2,
								// FS_TYPE_FILE_DEL = xxx, we don't need to store deleted
								// files because we take a new snap every time
								FS_TYPE_FILE_UNC = 3;

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

	typedef std::set<std::string>				filelist_t;

	typedef std::vector<uint8_t>				buffer_t;

	typedef struct bspatch_stream				bspatch_stream_t;

	typedef struct bsdiff_stream				bsdiff_stream_t;

	struct bspatch_s {
		size_t		idx;
		const buffer_t&	data;

		bspatch_s(const buffer_t& d) : idx(0), data(d) {
		}
	};

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
		ar_next_path = combine_paths(p, FS_ARCHIVE_BASE) + std::to_string(time(0)) + ".zip";
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

	fsarc_stat64_t fsarc_stat64_from_stat64(const struct stat64& s, const char* prev = 0, const uint32_t type = FS_TYPE_FILE_NEW) {
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
		static const char	NO_DATA;
		zip_t			*z_;
		fileset_t		f_map_;

		zip_f();
		zip_f(const zip_f&);
		zip_f& operator=(const zip_f&);

		bool add_data(zip_source_t *p_zf, const std::string& f, const char *prev, const uint32_t type) {
			if(f_map_.find(f) != f_map_.end()) {
				LOG_WARNING << "Couldn't add file '" << f << "' to archive " << z_ << "; already existing";
				return false;
			}
			const zip_int64_t idx = zip_file_add(z_, f.c_str(), p_zf, ZIP_FL_ENC_GUESS);
			if(-1 == idx) {
				zip_source_free(p_zf);
				throw fsarchive::rt_error("Can't add file/data ") << f << " (type " << type << ") to the archive";
			}
			// https://libzip.org/documentation/zip_file_extra_field_set.html
			// we can't use the info libzip stamps because the mtime is off
			// by one usecond, plus we need to store additional metadata
			struct stat64 s = {0};
			if(lstat64(f.c_str(), &s))
				throw fsarchive::rt_error("Invalid/unable to lstat64 file: ") << f;
			const auto fs_t = fsarc_stat64_from_stat64(s, prev, type);
			if(zip_file_extra_field_set(z_, idx, FS_ZIP_EXTRA_FIELD_ID, 0, (const zip_uint8_t*)&fs_t, sizeof(fs_t), ZIP_FL_LOCAL))
				throw fsarchive::rt_error("Can't set extra field FS_ZIP_EXTRA_FIELD_ID for file ") << f;
			f_map_[f] = fs_t;
			LOG_INFO << "File/data '" << f << "' (type " << type << ") added to archive " << z_;
			return true;
		}
	public:
		zip_f(const std::string& fname, const bool ro) : z_(zip_open(fname.c_str(), (ro) ? ZIP_RDONLY : (ZIP_CREATE | ZIP_EXCL), 0)) {
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
			LOG_INFO << "Opened zip '" <<  fname << "' with " << f_map_.size() << " entries, id " << z_;
		}

		bool add_new_file(const std::string& f) {
			zip_source_t	*p_zf = zip_source_file_create(f.c_str(), 0, -1, 0);
			if(!p_zf)
				throw fsarchive::rt_error("Can't open source file for zip ") << f;
			return add_data(p_zf, f, 0, FS_TYPE_FILE_NEW);
		}

		bool add_bsdiff(const std::string& f, const std::string& diff, const char* prev) {
			// in short, we have to duplicate the buffer...
			// https://stackoverflow.com/questions/73820283/add-multiple-files-from-buffers-to-zip-archive-using-libzip
			// https://stackoverflow.com/questions/73721970/how-to-construct-a-zip-file-with-libzip
			// https://stackoverflow.com/questions/74988236/can-libzip-reliably-write-in-zip-archives-from-buffers-memory-not-files
			// this is not great...
			uint8_t		*dup_diff = (uint8_t*)malloc(diff.size());
			if(!dup_diff)
				throw fsarchive::rt_error("Can't duplicate buffer to insert diff file in zip ") << f;
			memcpy(dup_diff, diff.data(), diff.size());
			zip_source_t	*p_zf = zip_source_buffer(z_, (const void*)dup_diff, diff.size(), 1);
			if(!p_zf) {
				free(dup_diff);
				throw fsarchive::rt_error("Can't create buffer for diff file for zip ") << f;
			}
			return add_data(p_zf, f, prev, FS_TYPE_FILE_MOD);
		}

		bool add_unchanged(const std::string& f, const char* prev) {
			zip_source_t	*p_zf = zip_source_buffer(z_, (const void*)&NO_DATA, 0, 0);
			if(!p_zf)
				throw fsarchive::rt_error("Can't create buffer for unchanged file for zip ") << f;
			return add_data(p_zf, f, prev, FS_TYPE_FILE_UNC);
		}

		bool extract_file(const std::string& f, buffer_t& data, fsarc_stat64_t& stat) const {
			const auto it_f = f_map_.find(f);
			if(f_map_.end() == it_f) {
				LOG_WARNING << "Can't extract/find file '" << f << "' in archive " << z_;
				return false;
			}
			stat = it_f->second;
			const auto z_idx = zip_name_locate(z_, f.c_str(), 0);
			if(-1 == z_idx)
				throw fsarchive::rt_error("Can't locate file ") << f << " in archive";
			zip_stat_t s = {0};
			if(zip_stat_index(z_, z_idx, 0, &s))
				throw fsarchive::rt_error("Can't zip_stat_index file ") << f << " in archive";
			data.resize(s.size);
			std::unique_ptr<zip_file_t, void (*)(zip_file_t*)> z_file(
				zip_fopen_index(z_, z_idx, 0),
				[](zip_file_t* z){ if(z) zip_fclose(z); }
			);
			const auto rb = zip_fread(z_file.get(), (void*)data.data(), data.size());
			if(rb < 0 || (uint64_t)rb != s.size)
				throw fsarchive::rt_error("Can't full zip_fread ") << f << " in archive";
			LOG_INFO << "File '" << f << "' extracted from archive " << z_;
			return true;
		}

		const fileset_t& get_fileset(void) const {
			return f_map_;
		}

		~zip_f() {
			zip_close(z_);
			LOG_SPAM << "Closed zip, id " << z_;
		}
	};

	const char	zip_f::NO_DATA = 0x00;

	template<typename fn_on_file>
	void r_file_find(const std::string& f, fn_on_file&& on_file) {
		struct stat64 s = {0};
		if(lstat64(f.c_str(), &s))
			throw fsarchive::rt_error("Invalid/unable to lstat64 file/directory: ") << f;
		if(!S_ISDIR(s.st_mode)) {
			if(S_ISREG(s.st_mode)) {
				on_file(f);
			}
		} else {
			std::unique_ptr<DIR, void (*)(DIR*)> p_dir(opendir(f.c_str()), [](DIR *d){ if(d) closedir(d);});
			struct dirent64	*de = 0;
			while((de = readdir64(p_dir.get()))) {
				if(std::string(".") == de->d_name ||
				   std::string("..") == de->d_name)
					continue;
				if(DT_REG == de->d_type || DT_DIR == de->d_type)
					r_file_find(combine_paths(f, de->d_name), on_file);
			}
		}
	}

	void r_rebuild_file(const zip_f& c_fs, const std::string& f, buffer_t& data) {
		using namespace fsarchive;

		fsarc_stat64_t	s = {0};
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
			const zip_f	p_fs(combine_paths(settings::AR_DIR, s.fs_prev), true);
			r_rebuild_file(p_fs, f, data);
			LOG_INFO << "File '" << f << "' has been forwarded as is (UNC) from " << s.fs_prev;
			return;
		} else if(FS_TYPE_FILE_MOD == s.fs_type) {
			// if the file is modified, we first need to
			// - get the original
			buffer_t	p_data;
			const zip_f	p_fs(combine_paths(settings::AR_DIR, s.fs_prev), true);
			r_rebuild_file(p_fs, f, p_data);
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
}

void fsarchive::init_update_archive(char *in_dirs[], const int n) {
	using namespace fsarchive;

	// let's check that we have a valid directory
	std::string	ar_next_path;
	filelist_t	ar_files;
	check_dir_fsarchives(settings::AR_DIR, ar_next_path, ar_files);
	// if we don't have any files, then write from scratch
	if(ar_files.empty()) {
		LOG_INFO << "Building an archive from scratch: " << ar_next_path;
		zip_f		z(ar_next_path.c_str(), false);
		auto fn_on_file = [&z](const std::string& s) -> void {
			z.add_new_file(s);
			LOG_INFO << "File '" << s << "' has been added as new (NEW)";
		};
		for(int i=0; i < n; ++i)
			r_file_find(in_dirs[i], fn_on_file);
	} else {
		// otherwise load the latest archive
		LOG_INFO << "Building a delta archive: " << *ar_files.rbegin() << " -> " << ar_next_path;
		const auto&	z_latest_name = *ar_files.rbegin();
		zip_f		z_latest(combine_paths(settings::AR_DIR, z_latest_name), true);
		// we need to generate a new 'delta' archive
		zip_f		z_next(ar_next_path.c_str(), false);
		// then we need to get all the files
		fileset_t	all_files;
		{
			auto fn_fileadd = [&all_files](const std::string& s) -> void {
				struct stat64 st = {0};
				if(lstat64(s.c_str(), &st))
					throw fsarchive::rt_error("Invalid/unable to lstat64 file: ") << s;
				all_files[s] = fsarc_stat64_from_stat64(st);
			};
			for(int i=0; i < n; ++i)
				r_file_find(in_dirs[i], fn_fileadd);
		}
		// then we should have 3 logical 'sets'
		// * new files
		// * mod(ified) files
		// * unc(changed) files
		// and we should manage accordingly
		const auto&	latest_fileset = z_latest.get_fileset();
		for(const auto& f : all_files) {
			const auto	it_latest = latest_fileset.find(f.first);
			if(it_latest == latest_fileset.end()) {
				// brand new file
				z_next.add_new_file(f.first);
				LOG_INFO << "File '" << f.first << "' has been added as new (NEW)";
			} else if((f.second.fs_mtime != it_latest->second.fs_mtime) ||
				  (f.second.fs_size != it_latest->second.fs_size)) {
				// changed file
				// first rebuild the file
				buffer_t	p_data;
				r_rebuild_file(z_latest, f.first, p_data);
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
				z_next.add_bsdiff(f.first, s_diff.str(), z_latest_name.c_str());
				LOG_INFO << "File '" << f.first << "' has been added as changed (MOD) -> " << z_latest_name;
			} else {
				// unchanged file
				// optimization - if the file is unchanged in z_latest as well,
				// then use its prev!
				const char *prev_unc = (FS_TYPE_FILE_UNC == it_latest->second.fs_type) ? it_latest->second.fs_prev : z_latest_name.c_str();
				z_next.add_unchanged(f.first, prev_unc);
				LOG_INFO << "File '" << f.first << "' has been added as unchanged (UNC) -> " << prev_unc;
			}
		}
	}
}

void fsarchive::restore_archive(void) {
	using namespace fsarchive;

	struct stat64 s = {0};
	if(settings::RE_FILE.empty() || lstat64(settings::RE_FILE.c_str(), &s) || !S_ISREG(s.st_mode))
		throw fsarchive::rt_error("Archive to restore is empty and/or file doesn't exist/is not accessible ") << settings::RE_FILE;

	zip_f	z(settings::RE_FILE, true);

	const auto&	re_fs = z.get_fileset();
	for(const auto& f : re_fs) {
		const std::string	out_file = (f.first[0] != '/' && !settings::RE_DIR.empty()) ? combine_paths(settings::RE_DIR + '/', f.first) : f.first;
		const auto		it_l_slash = out_file.find_last_of('/');
		if(it_l_slash != std::string::npos)
			init_paths(out_file.substr(0, it_l_slash+1));
		buffer_t	buf_file;
		r_rebuild_file(z, f.first, buf_file);
		std::ofstream	ostr(out_file, std::ios_base::binary);
		if((long int)buf_file.size() != ostr.write((const char*)buf_file.data(), buf_file.size()).tellp())
			throw fsarchive::rt_error("Can't restore file ") << f.first << " on the disk " << out_file;
	}
}

