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

#ifndef _SETTINGS_H_
#define _SETTINGS_H_

#include <cstdlib>
#include <string>
#include <set>

namespace fsarchive { 
	namespace settings {
		enum ACTION {
			A_ARCHIVE = 1,
			A_RESTORE = 2,
			A_NONE = -1
		};

		typedef std::set<std::string>	excllist_t;

		extern int		AR_ACTION;
		extern std::string	AR_DIR;
		extern std::string	RE_FILE;
		extern std::string	RE_DIR;
		extern int		AR_COMP_LEVEL;
		extern bool		AR_FORCE_NEW;
		extern excllist_t	AR_EXCLUSIONS;
		extern int64_t		AR_SZ_FILTER;
		extern bool		RE_METADATA;
		extern bool		DRY_RUN;
	}

	int parse_args(int argc, char *argv[], const char *prog, const char *version);
}

#endif //_SETTINGS_H_

