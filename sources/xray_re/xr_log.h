#ifndef __GNUC__
#pragma once
#endif
#ifndef __XR_LOG_H__
#define __XR_LOG_H__

#include <cstdarg>

namespace xray_re {

class xr_writer;

class xr_log {
public:
			xr_log();
			~xr_log();
	static xr_log&	instance();

	void		init(const char* name, const char* prefix = 0);

	void		diagnostic(const char* format, ...);
	void		diagnostic(const char* format, va_list ap);

	void		fatal(const char* msg, const char* file, unsigned line);

	// throw xr_error from fatal() instead of aborting (for host applications).
	void		set_throw_on_fatal(bool value);

private:
	char		m_prefix[256];
	FILE*		m_log;
	bool		m_throw_on_fatal;
};

inline xr_log::xr_log(): m_log(NULL), m_throw_on_fatal(false) { m_prefix[0] = '\0'; }

inline void xr_log::set_throw_on_fatal(bool value) { m_throw_on_fatal = value; }

inline xr_log& xr_log::instance()
{
	static xr_log instance0;
	return instance0;
}

} // end of namespace xray_re

#endif
