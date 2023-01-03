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

#include <sstream>
#include <chrono>

namespace fsarchive {
	namespace log {
		enum TYPE {
			T_SPAM = 1,
			T_INFO = 2,
			T_WARNING = 4,
			T_ERROR = 8
		};

		enum LEVEL {
			L_ERROR = T_ERROR,
			L_WARNING = L_ERROR|T_WARNING,
			L_INFO = L_WARNING|T_INFO,
			L_SPAM = L_INFO|T_SPAM
		};

		extern int level;

		extern void set_level(LEVEL l);

		class message {
			const TYPE		t_;
			const std::chrono::time_point<std::chrono::high_resolution_clock>	tp_;
			std::stringstream	msg_;

			message();
			message(const message&);
			message& operator=(const message&);
		public:
			message(TYPE t);

			template<typename T>
			message& operator<<(const T& rhs) {
				if(!(level & t_))
					return *this;
				msg_ << rhs;

				return *this;
			}

			~message();
		};
		
	}
}

#define LOG_SPAM fsarchive::log::message(fsarchive::log::TYPE::T_SPAM)
#define LOG_INFO fsarchive::log::message(fsarchive::log::TYPE::T_INFO)
#define LOG_WARNING fsarchive::log::message(fsarchive::log::TYPE::T_WARNING)
#define LOG_ERROR fsarchive::log::message(fsarchive::log::TYPE::T_ERROR)

