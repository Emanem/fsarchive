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

#include <exception>
#include "settings.h"
#include "fsarchive.h"
#include "log.h"

namespace {
	const char*	__version__ = "0.1";
}

int main(int argc, char *argv[]) {
	try {
		const int args_idx = fsarchive::parse_args(argc, argv, argv[0], __version__);
		if (fsarchive::settings::AR_ADD) {
			fsarchive::init_update_archive(argv + args_idx, argc - args_idx);
		} else {
			// in case the name of RE_FILE contains a '/'
			// set the same for AR_DIR
			const auto	it_l_slash = fsarchive::settings::RE_FILE.find_last_of('/');
			if(it_l_slash != std::string::npos)
				fsarchive::settings::AR_DIR = fsarchive::settings::RE_FILE.substr(0, it_l_slash+1);
			fsarchive::restore_archive();
		}
	} catch(const std::exception& e) {
		LOG_ERROR << "Exception: " << e.what();
		return 1;
	} catch(...) {
		LOG_ERROR << "Unknown exception";
		return 1;
	}
}

