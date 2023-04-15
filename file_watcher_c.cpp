#include "file_watcher_c.h"

//

bool get_last_modifyed(const std::wstring& fname, FILETIME& ftime)
{
	WIN32_FILE_ATTRIBUTE_DATA file_info;

	if (GetFileAttributesExW(fname.c_str(), GET_FILEEX_INFO_LEVELS::GetFileExInfoStandard, &file_info))
	{
		ftime = file_info.ftLastWriteTime;
		return true;
	}

	// if failed - try to get filetime by another way

	HANDLE hFile = CreateFileW(fname.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, 0, NULL);

	if (hFile != INVALID_HANDLE_VALUE)
	{
		FILETIME ftCreate, ftAccess;
		if (GetFileTime(hFile, &ftCreate, &ftAccess, &ftime) == TRUE)
		{
			CloseHandle(hFile);
			return true;
		}

		CloseHandle(hFile);
	}

	return false;
}


file_watcher_c::file_watcher_c()
	: m_service_io_port_handle(INVALID_HANDLE_VALUE)
{
	m_callback = [](std::wstring, std::wstring, file_watcher_c::e_action) -> void {};
}


file_watcher_c::~file_watcher_c()
{
	stop_watching();
}


void file_watcher_c::register_callback(std::function<void(std::wstring, std::wstring, file_watcher_c::e_action)> func)
{
	auto fictive_func = [](std::wstring, std::wstring, file_watcher_c::e_action) -> void {};
	m_callback = (func == nullptr) ?
		fictive_func : func;
}


file_watcher_c::dir_watcher_c* file_watcher_c::register_directory(const std::wstring& path, DWORD notify_filters)
{
	if (notify_filters > e_notify::NOTIFY_ALL)
	{
		return nullptr;
	}

	if (m_service_io_port_handle == INVALID_HANDLE_VALUE || m_service_io_port_handle == NULL)
	{
		m_service_io_port_handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
		if (m_service_io_port_handle == NULL)
		{
			return nullptr;
		}
	}

	watcher_ptr_t new_watcher(new file_watcher_c::dir_watcher_c(this));

	new_watcher->set_filters(notify_filters);
	if (new_watcher->init(path))
	{
		auto watcher_ptr = new_watcher.get();
		m_watchers.emplace_back(std::move(new_watcher));

		SetEvent(m_management_events.work_event_handle);

		return watcher_ptr;
	}

	return nullptr;
}


bool file_watcher_c::unregister_directory(const file_watcher_c::dir_watcher_c* watcher)
{
	for (auto watcher_it = m_watchers.begin(); watcher_it != m_watchers.end(); ++watcher_it)
	{
		if (watcher_it->get() == watcher)
		{
			m_watchers.erase(watcher_it);

			if (m_watchers.size() == 0)
			{
				ResetEvent(m_management_events.work_event_handle);
			}

			return true;
		}
	}
	return false;
}


bool file_watcher_c::start_watching()
{
	ResetEvent(m_management_events.shutdown_event_handle);

	if (m_watchers.size() == 0
		|| m_service_io_port_handle == INVALID_HANDLE_VALUE
		|| m_service_io_port_handle == NULL)
	{
		ResetEvent(m_management_events.work_event_handle);
	}

	for (auto& watcher : m_watchers)
	{
		watcher->read_async();
	}

	m_main_thread = std::thread(&file_watcher_c::run, this);

	return true;
}


bool file_watcher_c::stop_watching()
{
	if (!m_main_thread.joinable())
		return false;

	m_running = false;

	SetEvent(m_management_events.shutdown_event_handle);
	CloseHandle(m_service_io_port_handle);

	if (m_main_thread.joinable())
	{
		m_main_thread.join();
	}

	m_watchers.clear();

	return true;
}


void file_watcher_c::run()
{
	m_running = true;

	//
	DWORD no_of_bytes = 0;
	ULONG_PTR key = 0;
	EXTENDED_OVERLAPPED* overlap = nullptr;

	//
	while (m_running)
	{
		DWORD wait_result = WaitForMultipleObjects(2, (const HANDLE*)&m_management_events, FALSE, INFINITE);
		if (wait_result == WAIT_OBJECT_0 + 1) // shutdown event
		{
			continue; // (m_running is already false)
		}

		overlap = nullptr;
		BOOL success = GetQueuedCompletionStatus(m_service_io_port_handle, &no_of_bytes, &key, (LPOVERLAPPED*)&overlap, INFINITE);

		if (overlap == nullptr || success != TRUE || no_of_bytes == 0)
		{
			if (m_running)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}

			continue;
		}

		file_watcher_c::dir_watcher_c* const watcher = overlap->watcher_ptr;
		watcher->process_async_request();
		watcher->read_async();
	}

	m_running = false;
}


HANDLE file_watcher_c::get_service_io() const
{
	return m_service_io_port_handle;
}


std::function<void(std::wstring, std::wstring, file_watcher_c::e_action)> file_watcher_c::get_callback() const
{
	return m_callback;
}


// ============================================================================ //


file_watcher_c::dir_watcher_c::dir_watcher_c(file_watcher_c* owner)
	: m_owner(owner)
	, m_directory_handle(INVALID_HANDLE_VALUE)
	, m_io_port_handle(INVALID_HANDLE_VALUE)
	, m_filters(e_notify::NOTIFY_ALL)
{
}


file_watcher_c::dir_watcher_c::~dir_watcher_c()
{
	if (m_directory_handle != INVALID_HANDLE_VALUE && m_directory_handle != NULL)
	{
		CancelIo(m_directory_handle);
		CloseHandle(m_directory_handle);
	}

	m_file_cache.clear();
}


void file_watcher_c::dir_watcher_c::register_callback(std::function<void(std::wstring, std::wstring, file_watcher_c::e_action)> func)
{
	auto fictive_func = [](std::wstring, std::wstring, file_watcher_c::e_action) -> void {};

	m_callback = (func == nullptr) ? fictive_func : func;
}


bool file_watcher_c::dir_watcher_c::set_filters(DWORD filters)
{
	if (filters > e_notify::NOTIFY_ALL)
	{
		return false;
	}

	::InterlockedExchange(&m_filters, filters);
	return true;
}


bool file_watcher_c::dir_watcher_c::add_pattern(const std::wstring& regex)
{
	try
	{
		m_patterns.emplace_back(std::wregex(regex));
	}
	catch (...)
	{
		return false;
	}

	return true;
}


bool file_watcher_c::dir_watcher_c::check_for_pattern_matchs(const std::wstring& fname)
{
	if (m_patterns.size() == 0)
	{
		return true;
	}

	try
	{
		for (auto& pattern : m_patterns)
		{
			std::wsmatch match;
			if (std::regex_search(fname, match, pattern) && match.size() > 1)
			{
				return true;
			}
		}
	}
	catch (...)
	{
	}

	return false;
}


DWORD file_watcher_c::dir_watcher_c::get_filters() const
{
	return m_filters;
}


std::wstring file_watcher_c::dir_watcher_c::get_directory_path() const
{
	return m_directory_path.c_str();
}


bool file_watcher_c::dir_watcher_c::read_async()
{
	BOOL async_operation_started = ReadDirectoryChangesW(m_directory_handle, (void*)m_notifications, sizeof(m_notifications), FALSE,
		m_filters, NULL, m_extended_overlap.get(), NULL);

	return (async_operation_started == TRUE);
}


bool file_watcher_c::dir_watcher_c::process_async_request()
{
	FILE_NOTIFY_INFORMATION* notify = (FILE_NOTIFY_INFORMATION*)m_notifications;

	while (true)
	{
		DWORD action = notify->Action;
		std::wstring fname(notify->FileName, notify->FileName + notify->FileNameLength / sizeof(wchar_t));

		if (check_for_pattern_matchs(fname))
		{
			const auto& cached_result = m_file_cache.find(fname);
			if (cached_result != m_file_cache.cend())
			{
				auto& cached_data = cached_result->second;
				if (action != cached_data.first)
				{
					m_callback(m_directory_path, fname, (e_action)action);
				}

				//
				m_file_cache.erase(cached_result);
			}
			else
			{
				if (action == e_action::MODIFIED)
				{
					m_file_cache.emplace(fname, std::make_pair(action, FILETIME()));
				}

				m_callback(m_directory_path, fname, (e_action)action);
			}
		}

		if (notify->NextEntryOffset == 0)
		{
			break;
		}

		//
		notify = (FILE_NOTIFY_INFORMATION*)((char*)notify + notify->NextEntryOffset);
	}

	return true;
}


bool file_watcher_c::dir_watcher_c::init(const std::wstring& directory)
{
	HANDLE service_io_port_handle = m_owner->get_service_io();

	m_directory_handle = CreateFileW
	(
		directory.c_str(),
		FILE_LIST_DIRECTORY,
		FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
		NULL
	);

	if (m_directory_handle == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	m_directory_path = directory;

	SYSTEM_INFO sys_info;
	::GetSystemInfo(&sys_info);

	m_io_port_handle = CreateIoCompletionPort(m_directory_handle, service_io_port_handle, 0, 0);
	if (m_io_port_handle == NULL)
	{
		return false;
	}

	m_extended_overlap.reset(new EXTENDED_OVERLAPPED);
	{
		m_extended_overlap->watcher_ptr = this;
		m_extended_overlap->hEvent = CreateEvent(NULL, FALSE, TRUE, __T("Local\\CFileWatcher::CDirWatcher"));
		_ASSERT(m_extended_overlap->hEvent != NULL);
	}

	m_callback = m_owner->get_callback();

	return true;
}
