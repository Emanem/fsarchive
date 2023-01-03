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

#include "log.h"
#include <iostream>

int fsarchive::log::level = fsarchive::log::L_INFO;

void fsarchive::log::set_level(LEVEL l) {
	level = l;
}

fsarchive::log::message::message(TYPE t) : t_(t), tp_(std::chrono::high_resolution_clock::now()) {
}

fsarchive::log::message::~message() {
	if(!(level & t_))
		return;

	using namespace std::chrono;
	const auto	tp_tm = time_point_cast<system_clock::duration>(tp_);
	const auto	tm_t = system_clock::to_time_t(tp_tm);
	struct tm	res = {0};
	localtime_r(&tm_t, &res);
	char		tm_fmt[32],
			tm_buf[32];
	std::sprintf(tm_fmt, "%%Y-%%m-%%dT%%H:%%M:%%S.%03i", static_cast<int>(duration_cast<milliseconds>(tp_.time_since_epoch()).count()%1000));
	std::strftime(tm_buf, sizeof(tm_buf), tm_fmt, &res);

	std::cout << tm_buf << " [" << t_ << "] " << msg_.str() << std::endl;
}

