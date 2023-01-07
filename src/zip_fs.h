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

#ifndef _ZIP_FS_H_
#define _ZIP_FS_H_

#include <zip.h>
#include <unordered_map>
#include <set>
#include <vector>
#include <string>

namespace fsarchive {
	extern const char					*FS_ARCHIVE_BASE;
	
	const zip_uint16_t					FS_ZIP_EXTRA_FIELD_ID = 0xe0e0;
	
	const uint32_t						FS_TYPE_FILE_NEW = 1,
								FS_TYPE_FILE_MOD = 2,
								// FS_TYPE_FILE_DEL = xxx, we don't need to store deleted
								// files because we take a new snap every time
								FS_TYPE_FILE_UNC = 3;

	typedef struct _stat64 {
		mode_t fs_mode;
		uid_t fs_uid;
		gid_t fs_gid;
		uint32_t fs_type;
		time_t fs_atime;
		time_t fs_mtime;
		time_t fs_ctime;
		off64_t fs_size;
		char	fs_prev[32];
	} stat64_t;

	static_assert(sizeof(stat64_t) == (48 + 32), "sizeof(stat64_t) is not 48 + 32 bytes");

	typedef std::unordered_map<std::string, stat64_t>	fileset_t;

	typedef std::set<std::string>				filelist_t;

	typedef std::vector<uint8_t>				buffer_t;

	class zip_fs {
		static const char	NO_DATA;
		zip_t			*z_;
		const bool		ro_;
		fileset_t		f_map_;

		zip_fs();
		zip_fs(const zip_fs&);
		zip_fs& operator=(const zip_fs&);

		bool add_data(zip_source_t *p_zf, const std::string& f, const stat64_t& fs, const char *prev, const uint32_t type);
	public:
		zip_fs(const std::string& fname, const bool ro);

		bool add_file_new(const std::string& f, const stat64_t& fs);

		bool add_file_bsdiff(const std::string& f, const stat64_t& fs, const std::string& diff, const char* prev);

		bool add_file_unchanged(const std::string& f, const stat64_t& fs, const char* prev);

		bool add_directory(const std::string& d, const stat64_t& fs);

		bool extract_file(const std::string& f, buffer_t& data, stat64_t& stat) const;

		const fileset_t& get_fileset(void) const;

		~zip_fs();
	};
}

#endif //_ZIP_FS_H_

