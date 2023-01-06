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

#include "zip_fs.h"
#include "log.h"
#include "utils.h"
#include "settings.h"
#include <string.h>
#include <memory>

namespace {
	extern "C" void progress_cb(zip_t *arc, double p, void* usr_ptr) {
		fsarchive::log::progress	*l_p = (fsarchive::log::progress*)usr_ptr;
		l_p->update_completion(p);
	}
}

bool fsarchive::zip_fs::add_data(zip_source_t *p_zf, const std::string& f, const fsarchive::stat64_t& fs, const char *prev, const uint32_t type) {
	if(f_map_.find(f) != f_map_.end()) {
		LOG_WARNING << "Couldn't add file '" << f << "' to archive " << z_ << "; already existing";
		return false;
	}
	const zip_int64_t idx = zip_file_add(z_, f.c_str(), p_zf, ZIP_FL_ENC_GUESS);
	if(-1 == idx) {
		zip_source_free(p_zf);
		throw fsarchive::rt_error("Can't add file/data ") << f << " (type " << type << ") to the archive";
	}
	if(zip_set_file_compression(z_, idx, ZIP_CM_DEFLATE, (zip_uint32_t)settings::AR_COMP_LEVEL))
		throw fsarchive::rt_error("Can't set compression level for file/data ") << f << " (type " << type << ") to the archive";
	// https://libzip.org/documentation/zip_file_extra_field_set.html
	// we can't use the info libzip stamps because the mtime is off
	// by one usecond, plus we need to store additional metadata
	stat64_t fs_t = fs;
	if(prev) {
		strncpy(fs_t.fs_prev, prev, 31);
		fs_t.fs_prev[31] = '\0';
	} else {
		fs_t.fs_prev[0] = '\0';
	}
	fs_t.fs_type = type;
	if(zip_file_extra_field_set(z_, idx, FS_ZIP_EXTRA_FIELD_ID, 0, (const zip_uint8_t*)&fs_t, sizeof(fs_t), ZIP_FL_LOCAL))
		throw fsarchive::rt_error("Can't set extra field FS_ZIP_EXTRA_FIELD_ID for file ") << f;
	f_map_[f] = fs_t;
	LOG_SPAM << "File/data '" << f << "' (type " << type << ") added to archive " << z_;
	return true;
}

fsarchive::zip_fs::zip_fs(const std::string& fname, const bool ro) : z_(zip_open(fname.c_str(), (ro) ? ZIP_RDONLY : (ZIP_CREATE | ZIP_EXCL), 0)) {
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
			f_map_[st.name] = *(stat64_t*)pf;
		} else {
			zip_close(z_);
			throw fsarchive::rt_error("Couldn't find FS_ZIP_EXTRA_FIELD_ID for file ") << st.name;
		}
	}
	LOG_INFO << "Opened zip '" <<  fname << "' with " << f_map_.size() << " entries, id " << z_ << ((ro) ? " (R/O)" : " (W/O)");
}

bool fsarchive::zip_fs::add_file_new(const std::string& f, const fsarchive::stat64_t& fs) {
	zip_source_t	*p_zf = zip_source_file_create(f.c_str(), 0, -1, 0);
	if(!p_zf)
		throw fsarchive::rt_error("Can't open source file for zip ") << f;
	return add_data(p_zf, f, fs, 0, FS_TYPE_FILE_NEW);
}

bool fsarchive::zip_fs::add_file_bsdiff(const std::string& f, const fsarchive::stat64_t& fs, const std::string& diff, const char* prev) {
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
	return add_data(p_zf, f, fs, prev, FS_TYPE_FILE_MOD);
}

bool fsarchive::zip_fs::add_file_unchanged(const std::string& f, const fsarchive::stat64_t& fs, const char* prev) {
	zip_source_t	*p_zf = zip_source_buffer(z_, (const void*)&NO_DATA, 0, 0);
	if(!p_zf)
		throw fsarchive::rt_error("Can't create buffer for unchanged file for zip ") << f;
	return add_data(p_zf, f, fs, prev, FS_TYPE_FILE_UNC);
}

bool fsarchive::zip_fs::add_directory(const std::string& d, const fsarchive::stat64_t& fs) {
	const auto d_idx = zip_dir_add(z_, d.c_str(), ZIP_FL_ENC_GUESS);
	if(-1 == d_idx)
		throw fsarchive::rt_error("Can't add directory ") << d << " to archive";
	if(zip_file_extra_field_set(z_, d_idx, FS_ZIP_EXTRA_FIELD_ID, 0, (const zip_uint8_t*)&fs, sizeof(fs), ZIP_FL_LOCAL))
		throw fsarchive::rt_error("Can't set extra field FS_ZIP_EXTRA_FIELD_ID for directory ") << d;
	f_map_[d] = fs;
	LOG_SPAM << "Directory '" << d << "' added to archive " << z_;
	return true;
}

bool fsarchive::zip_fs::extract_file(const std::string& f, fsarchive::buffer_t& data, fsarchive::stat64_t& stat) const {
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
	LOG_SPAM << "File '" << f << "' extracted from archive " << z_;
	return true;
}

const fsarchive::fileset_t& fsarchive::zip_fs::get_fileset(void) const {
	return f_map_;
}

fsarchive::zip_fs::~zip_fs() {
	fsarchive::log::progress	p("Archiving zip file");
	zip_register_progress_callback_with_state(z_, 0.0001, progress_cb, 0, &p);
	zip_close(z_);
	LOG_SPAM << "Closed zip, id " << z_;
}

const char	*fsarchive::FS_ARCHIVE_BASE = "fsarc_";

const char	fsarchive::zip_fs::NO_DATA = 0x00;
