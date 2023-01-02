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

#ifndef _UTILS_H_
#define _UTILS_H_

#include <exception>
#include <string>
#include <sstream>
#include <sys/time.h>

namespace fsarchive {

        class rt_error : public std::exception {
                std::ostringstream      _oss;
                std::string             _str;
public:
                rt_error(const char *e) throw() {
                        _oss << e;
                        _str = _oss.str();
                }

                rt_error(const rt_error& rhs) throw() {
                        _oss.str(rhs._oss.str());
                        _str = _oss.str();
                }

                rt_error& operator=(const rt_error& rhs) throw() {
                        _oss.str(rhs._oss.str());
                        _str = _oss.str();

                        return *this;
                }

                template<typename T>
                rt_error& operator<<(const T& in) {
                        _oss << in;
                        _str = _oss.str();

                        return *this;
                }

                virtual const char* what() const throw() {
                        return _str.c_str();
                }

                virtual ~rt_error() throw() {
                }
        };

	inline double tv_to_sec(const timeval& tv) {
		return 1.0*tv.tv_sec + (1.0/1000000.0)*tv.tv_usec;
	}
}

#endif //_UTILS_H_

