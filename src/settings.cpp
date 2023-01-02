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

		std::cerr <<	"Usage: " << prog << " [options]\nExecutes fsarchive " << version << "\n\n"
				"-a, --archive (dir) Archives all input files and directories inside (dir)/fsarchive_main.zip\n"
				"                    and/or updates existing archives generating a (dir)/fsarchive_<timestamp>.zip\n"
				"    --help          Prints this help and exit\n\n"
		<< std::flush;
	}
}

namespace fsarchive { 
	namespace settings {
		bool		AR_ADD = true;
		std::string	AR_DIR = "";
	}
}

int fsarchive::parse_args(int argc, char *argv[], const char *prog, const char *version) {
	using namespace fsarchive::settings;

	int			c;
	static struct option	long_options[] = {
		{"help",	no_argument,	   0,	0},
		{"archive",	required_argument, 0,	'a'},
		{0, 0, 0, 0}
	};
	
	while (1) {
        	// getopt_long stores the option index here
        	int		option_index = 0;

		if(-1 == (c = getopt_long(argc, argv, "ha:", long_options, &option_index)))
       			break;

		switch (c) {
		case 0: {
			// If this option set a flag, do nothing else now
           		if (long_options[option_index].flag != 0)
                		break;
			if(!std::strcmp("help", long_options[option_index].name)) {
				print_help(prog, version);
				std::exit(0);
			}
		} break;

		case 'a': {
			AR_DIR = optarg;
			AR_ADD = true;
		} break;

		case '?':
		break;
		
		default:
			throw rt_error("Invalid option '") << (char)c << "'";
		break;
             	}
	}

	return optind;
}

