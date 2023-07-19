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

private:
	char		m_prefix[256];
	FILE*		m_log;
};

inline xr_log::xr_log(): m_log(NULL) { m_prefix[0] = '\0'; }

inline xr_log& xr_log::instance()
{
	static xr_log instance0;
	return instance0;
}

} // end of namespace xray_re

#endif
