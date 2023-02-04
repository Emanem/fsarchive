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
#include "utils.h"
#include <iostream>
#include <sys/ioctl.h>
#include <unistd.h>
#include <signal.h>

namespace {
	struct winsize term_size = {0};

	void sig_term_resize(int s) {
		ioctl(STDOUT_FILENO, TIOCGWINSZ, &term_size);
	}

	bool init_term(void) {
		if(!isatty(STDOUT_FILENO))
			return false;

		signal(SIGWINCH, sig_term_resize);
		ioctl(STDOUT_FILENO, TIOCGWINSZ, &term_size);
		return true;
	}

	const bool is_term = init_term();

	void get_header(const std::chrono::time_point<std::chrono::high_resolution_clock>& tp, char out[32]) {
		using namespace std::chrono;
		const auto	tp_tm = time_point_cast<system_clock::duration>(tp);
		const auto	tm_t = system_clock::to_time_t(tp_tm);
		struct tm	res = {0};
		localtime_r(&tm_t, &res);
		char		tm_fmt[32];
		std::sprintf(tm_fmt, "%%Y-%%m-%%dT%%H:%%M:%%S.%03i", static_cast<int>(duration_cast<milliseconds>(tp.time_since_epoch()).count()%1000));
		std::strftime(out, sizeof(char)*32, tm_fmt, &res);
	}

	fsarchive::log::progress	*cur_prg = 0;

	void do_print(const std::string& log_line, const bool progress_only) {
		if(!is_term) {
			if(!progress_only)
				printf("%s\n", log_line.c_str());
			return;
		}

		if(!progress_only) {
			printf("\r%s\n", log_line.c_str());
		}
		if(cur_prg) {
			printf("\r[%s %6.2f%%]", cur_prg->get_label().c_str(), 100.0*cur_prg->get_completion());
		}
		fflush(stdout);
	}
}

int fsarchive::log::level = fsarchive::log::L_INFO;

void fsarchive::log::set_level(LEVEL l) {
	level = l;
}

fsarchive::log::message::message(TYPE t) : t_(t), tp_(std::chrono::high_resolution_clock::now()) {
}

fsarchive::log::message::~message() {
	if(!(level & t_))
		return;

	const std::string s_msg = msg_.str();
	if(!s_msg.empty()) {
		char	tm_buf[32];
		get_header(tp_, tm_buf);

		std::stringstream ss;
		ss << tm_buf << " [" << t_ << "] " << s_msg;
		do_print(ss.str(), false);
	}
}

fsarchive::log::progress::progress(const std::string& label) : label_(label), completion_(.0) {
	if(!cur_prg)
		cur_prg = this;
	else
		throw rt_error("Can't set progress log, already set to ") << cur_prg->label_;
}

void fsarchive::log::progress::update_completion(const double c) {
	completion_ = c;
	do_print("", true);
}

void fsarchive::log::progress::reset_completion(const double c) {
	completion_ = c;
	do_print("", true);
	cur_prg = 0;
	do_print("", false);
	completion_ = .0;
}

const std::string& fsarchive::log::progress::get_label(void) const {
	return label_;
}

const double fsarchive::log::progress::get_completion(void) const {
	return completion_;
}

fsarchive::log::progress::~progress() {
	cur_prg = 0;
	if(completion_ != .0)
		do_print("", false);
}

