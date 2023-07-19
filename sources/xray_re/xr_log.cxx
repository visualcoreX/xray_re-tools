#include <cstdlib>
#include <cstring>
#include <iostream>
#include "xr_log.h"
#include "xr_file_system.h"
#if defined(DEBUG) && defined(WIN32)
#  include <Windows.h>// OutputDebugString
#endif

using namespace xray_re;

xr_log::~xr_log()
{
	if (m_log)
		fclose(m_log);
}

void xr_log::init(const char* name, const char* prefix)
{
	xr_file_system& fs = xr_file_system::instance();
	std::string path;
	if (fs.resolve_path(PA_LOGS, name, path))
		m_log = fopen(path.append(".log").c_str(), "w");
	if (prefix) {
		strncpy(m_prefix, prefix, sizeof(m_prefix));
		m_prefix[sizeof(m_prefix)-1] = '\0';
	}
	if (m_log == 0)
		diagnostic("xray_re: log started (console only)");
	else
		diagnostic("xray_re: log started (console and %s.log)", name);
}

void xr_log::diagnostic(const char* format, va_list ap)
{
	if (m_log) {
		if (m_prefix[0]) {
			fputs(m_prefix, m_log);
			fputs(": ", m_log);
		}

		vfprintf(m_log, format, ap);
		fputs("\n", m_log);
	}

	vfprintf(stderr, format, ap);
	fputs("\n", stderr);

#if defined(DEBUG) && defined(WIN32)
	char tmpbuf[1024];
#if defined(_MSC_VER) && _MSC_VER >= 1400
	int n = vsprintf_s(tmpbuf, sizeof(tmpbuf), format, ap);
#else
	int n = vsnprintf(tmpbuf, sizeof(tmpbuf), format, ap);
#endif
	OutputDebugString(tmpbuf);
#endif
}

void xr_log::diagnostic(const char* format, ...)
{
	va_list ap;
	va_start(ap, format);
	diagnostic(format, ap);
	va_end(ap);
}

void xr_log::fatal(const char* message, const char* file, unsigned line)
{
	diagnostic("[bug] %s at %s:%u", message, file, line);
//	MessageBoxA(NULL, m_buf, "xray_re", MB_OK);
	std::abort();
}

void xray_re::dbg(const char* format, ...)
{
	va_list ap;
	va_start(ap, format);
	xr_log::instance().diagnostic(format, ap);
	va_end(ap);
}

void xray_re::msg(const char* format, ...)
{
	va_list ap;
	va_start(ap, format);
	xr_log::instance().diagnostic(format, ap);
	va_end(ap);
}

void xray_re::die(const char* message, const char* file, unsigned line)
{
	xr_log::instance().fatal(message, file, line);
}
