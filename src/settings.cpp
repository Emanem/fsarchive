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

#include <iostream>
#include <getopt.h>
#include <cstring>
#include <stdexcept>
#include "settings.h"
#include "utils.h"

namespace {
	// settings/options management
	void print_help(const char *prog, const char *version) {
		using namespace fsarchive::settings;

		std::cerr <<	"Usage: " << prog << " [options] dir1 dir2 ... \nExecutes fsarchive " << version << "\n\n"
				"-a, --archive (dir)     Archives all input files (dir1, dir2, ...) and directories inside\n"
				"                        (dir)/fsarchive_<timestamp>.zip and/or updates existing archives generating a new\n"
			        "                        and/or delta (dir)/fsarchive_<timestamp>.zip\n"
				"-r, --restore (arc)     Restores files from archive (arc) into current dir or ablsolute path if stored so\n"
				"                        Specify -d to allow another directory to be the target destination for the restore\n"
				"-d, --restore-dir (dir) Sets the restore directory to this location\n"
				"    --comp-level (l)    Sets the compression level to (l) (from 1 to 9) where 1 is fastest and 9 is best.\n"
				"                        0 is default\n"
				"    --force-new-arc     Flag to force the creation of a new archive (-a option) even if a previous already\n"
				"                        exists (i.e. no delta archive would be created)\n"
				"-x, --exclude (str)     Excludes from archiving all the files/directories which match (str); if you want\n"
				"                        to have a 'contain' search, do specify the \"*(str)*\" pattern (i.e. -x \"*abc*\"\n"
				"                        will exclude all the files/dirs which contain the sequence 'abc').\n"
				"                        Please note that the only wildcard supported is *, everything else will be interpreted\n"
				"                        as a literal character; you can specify multiple exclusions (i.e. -x ex1 -x ex2 ... )\n"
				"    --size-filter (sz)  Set a maximum file size filter of size (sz); has to be a positive value (bytes) and\n"
				"                        can have suffixes such as k, m and g to respectively interpret as KiB, MiB and GiB\n"
				"                        By default this is disabled\n"
				"    --help              Prints this help and exit\n\n"
		<< std::flush;
	}
}

namespace fsarchive { 
	namespace settings {
		int		AR_ACTION = A_NONE;
		std::string	AR_DIR = "";
		std::string	RE_FILE = "";
		std::string	RE_DIR = "";
		int		AR_COMP_LEVEL = 0;
		bool		AR_FORCE_NEW = false;
		excllist_t	AR_EXCLUSIONS;
		int64_t		AR_SZ_FILTER = -1;
	}
}

int fsarchive::parse_args(int argc, char *argv[], const char *prog, const char *version) {
	using namespace fsarchive::settings;

	int			c;
	static struct option	long_options[] = {
		{"help",	no_argument,	   0,	0},
		{"archive",	required_argument, 0,	'a'},
		{"restore",	required_argument, 0,	'r'},
		{"restore-dir",	required_argument, 0,	'd'},
		{"comp-level",	required_argument, 0,	0},
		{"force-new-arc",no_argument,	   0,	0},
		{"exclude",	required_argument, 0,	'x'},
		{"size-filter",	required_argument, 0,	0},
		{0, 0, 0, 0}
	};
	
	while (1) {
        	// getopt_long stores the option index here
        	int		option_index = 0;

		if(-1 == (c = getopt_long(argc, argv, "a:r:d:x:", long_options, &option_index)))
       			break;

		switch (c) {
		case 0: {
			// If this option set a flag, do nothing else now
           		if (long_options[option_index].flag != 0)
                		break;
			if(!std::strcmp("help", long_options[option_index].name)) {
				print_help(prog, version);
				std::exit(0);
			} else if(!std::strcmp("comp-level", long_options[option_index].name)) {
				AR_COMP_LEVEL = std::atoi(optarg);
				if(AR_COMP_LEVEL < 0 || AR_COMP_LEVEL > 9)
					AR_COMP_LEVEL = 0;
			} else if(!std::strcmp("force-new-arc", long_options[option_index].name)) {
				AR_FORCE_NEW = true;
			} else if(!std::strcmp("size-filter", long_options[option_index].name)) {
				char	*ptrend = 0;
				AR_SZ_FILTER = strtol(optarg, &ptrend, 10);
				if(*ptrend) {
					const char	unit = tolower(*ptrend);
					switch(unit) {
						case 'g':
							AR_SZ_FILTER *= 1024;
						case 'm':
							AR_SZ_FILTER *= 1024;
						case 'k':
							AR_SZ_FILTER *= 1024;
							break;
						default:
							// fall through and throw exception
							AR_SZ_FILTER = -1;
							break;
					}
				}
				if(AR_SZ_FILTER <= 0)
					throw fsarchive::rt_error("Invalid size filter provided: ") << optarg;
			}
		} break;

		case 'a': {
			AR_DIR = optarg;
			if(AR_ACTION != A_NONE)
				throw fsarchive::rt_error("Invalid combination of -a and -r options");
			AR_ACTION = A_ARCHIVE;
		} break;

		case 'r': {
			RE_FILE = optarg;
			if(AR_ACTION != A_NONE)
				throw fsarchive::rt_error("Invalid combination of -a and -r options");
			AR_ACTION = A_RESTORE;
			// in case the name of RE_FILE contains a '/'
			// set the same for AR_DIR
			const auto	it_l_slash = fsarchive::settings::RE_FILE.find_last_of('/');
			if(it_l_slash != std::string::npos)
				fsarchive::settings::AR_DIR = fsarchive::settings::RE_FILE.substr(0, it_l_slash+1);
		} break;

		case 'd': {
			RE_DIR = optarg;
		} break;

		case 'x': {
			AR_EXCLUSIONS.insert(optarg);
		} break;

		case '?':
			return -1;
		break;
		
		default:
			throw rt_error("Invalid option '") << (char)c << "'";
		break;
             	}
	}

	return optind;
}

