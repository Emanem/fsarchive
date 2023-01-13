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
#include "utils.h"

namespace {
	const char*	__version__ = "0.2.0";
}

int main(int argc, char *argv[]) {
	try {
		const int args_idx = fsarchive::parse_args(argc, argv, argv[0], __version__);
		if(-1 == args_idx)
			return 1;
		switch(fsarchive::settings::AR_ACTION) {
			case fsarchive::settings::ACTION::A_ARCHIVE:
				fsarchive::init_update_archive(argv + args_idx, argc - args_idx);
				break;
			case fsarchive::settings::ACTION::A_RESTORE:
				fsarchive::restore_archive();
				break;
			default:
				throw fsarchive::rt_error("Invalid action ") << fsarchive::settings::AR_ACTION << " need to specify -a or -r";
		}
	} catch(const std::exception& e) {
		LOG_ERROR << "Exception: " << e.what();
		return 1;
	} catch(...) {
		LOG_ERROR << "Unknown exception";
		return 1;
	}
}

