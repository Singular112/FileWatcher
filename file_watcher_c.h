#pragma once

// system
#include <Windows.h>

// std
#include <map>
#include <regex>
#include <mutex>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <tchar.h>
#include <functional>

//

class file_watcher_c
{
	struct EXTENDED_OVERLAPPED;

#pragma pack(push, 1)
	struct management_events_s
	{
		HANDLE work_event_handle;
		HANDLE shutdown_event_handle;

		management_events_s()
		{
			work_event_handle = CreateEvent(NULL, TRUE, FALSE, NULL);
			shutdown_event_handle = CreateEvent(NULL, TRUE, FALSE, NULL);

			_ASSERT(work_event_handle != NULL);
			_ASSERT(shutdown_event_handle != NULL);
		}

		~management_events_s()
		{
			CloseHandle(work_event_handle);
			CloseHandle(shutdown_event_handle);
		}
	};
#pragma pack(pop)

public:
	/*
	# FILE_ACTION_ADDED					0x00000001   
	# FILE_ACTION_REMOVED				0x00000002   
	# FILE_ACTION_MODIFIED				0x00000003   
	# FILE_ACTION_RENAMED_OLD_NAME		0x00000004   
	# FILE_ACTION_RENAMED_NEW_NAME		0x00000005   
	*/
	enum e_action
	{
		ADDED				= FILE_ACTION_ADDED,
		REMOVED				= FILE_ACTION_REMOVED,
		MODIFIED			= FILE_ACTION_MODIFIED,
		RENAMED_OLD_NAME	= FILE_ACTION_RENAMED_OLD_NAME,
		RENAMED_NEW_NAME	= FILE_ACTION_RENAMED_NEW_NAME,

		ACTION_ALL			= FILE_ACTION_ADDED | FILE_ACTION_REMOVED | FILE_ACTION_MODIFIED
							| FILE_ACTION_RENAMED_OLD_NAME | FILE_ACTION_RENAMED_NEW_NAME
	};

	/*
	# FILE_NOTIFY_CHANGE_FILE_NAME		0x00000001
	# FILE_NOTIFY_CHANGE_DIR_NAME		0x00000002
	# FILE_NOTIFY_CHANGE_ATTRIBUTES		0x00000004
	# FILE_NOTIFY_CHANGE_SIZE			0x00000008
	# FILE_NOTIFY_CHANGE_LAST_WRITE		0x00000010
	# FILE_NOTIFY_CHANGE_LAST_ACCESS	0x00000020
	# FILE_NOTIFY_CHANGE_CREATION		0x00000040
	# FILE_NOTIFY_CHANGE_SECURITY		0x00000100
	*/
	enum e_notify
	{
		CHANGE_FILE_NAME	= FILE_NOTIFY_CHANGE_FILE_NAME,
		CHANGE_DIR_NAME		= FILE_NOTIFY_CHANGE_DIR_NAME,
		CHANGE_ATTRIBUTES	= FILE_NOTIFY_CHANGE_ATTRIBUTES,
		CHANGE_SIZE			= FILE_NOTIFY_CHANGE_SIZE,
		CHANGE_LAST_WRITE	= FILE_NOTIFY_CHANGE_LAST_WRITE,
		CHANGE_LAST_ACCESS	= FILE_NOTIFY_CHANGE_LAST_ACCESS,
		CHANGE_CREATION		= FILE_NOTIFY_CHANGE_CREATION,
		CHANGE_SECURITY		= FILE_NOTIFY_CHANGE_SECURITY,

		NOTIFY_ALL			= FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES
							| FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_LAST_ACCESS
							| FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_SECURITY
	};

public:	
	class dir_watcher_c
	{
		friend class file_watcher_c;

	private:
		dir_watcher_c(file_watcher_c* owner);
		bool init(const std::wstring& directory);
		bool read_async();
		bool process_async_request();

	public:
		~dir_watcher_c();

		void register_callback(std::function<void(std::wstring, std::wstring, file_watcher_c::e_action)> func);

		/*
		# FILE_NOTIFY_CHANGE_FILE_NAME		0x00000001
		# FILE_NOTIFY_CHANGE_DIR_NAME		0x00000002
		# FILE_NOTIFY_CHANGE_ATTRIBUTES		0x00000004
		# FILE_NOTIFY_CHANGE_SIZE			0x00000008
		# FILE_NOTIFY_CHANGE_LAST_WRITE		0x00000010
		# FILE_NOTIFY_CHANGE_LAST_ACCESS	0x00000020
		# FILE_NOTIFY_CHANGE_CREATION		0x00000040
		# FILE_NOTIFY_CHANGE_SECURITY		0x00000100
		*/
		bool set_filters(DWORD filters);
		bool add_pattern(const std::wstring& regex);
		bool check_for_pattern_matchs(const std::wstring& fname);
		DWORD get_filters() const;
		std::wstring get_directory_path() const;

	private:
		file_watcher_c* m_owner;
		HANDLE m_directory_handle;
		HANDLE m_io_port_handle;

		unsigned char m_notifications[10240];

		std::wstring m_directory_path;

		volatile DWORD m_filters;
		std::vector<std::wregex> m_patterns;

		struct extov_deleter
		{
			void operator()(EXTENDED_OVERLAPPED* ptr)
			{
				if (ptr->hEvent != INVALID_HANDLE_VALUE && ptr->hEvent != NULL)
					CloseHandle(ptr->hEvent);

				delete ptr;
				ptr = nullptr;
			}
		};
		std::unique_ptr<EXTENDED_OVERLAPPED, extov_deleter> m_extended_overlap;

		std::function<void(std::wstring, std::wstring, file_watcher_c::e_action)> m_callback;

		std::map<std::wstring, std::pair<DWORD, FILETIME>> m_file_cache;
	};

private:
	typedef std::shared_ptr<file_watcher_c::dir_watcher_c> watcher_ptr_t;

	struct EXTENDED_OVERLAPPED : public OVERLAPPED { file_watcher_c::dir_watcher_c* watcher_ptr; };

public:
	file_watcher_c();

	virtual ~file_watcher_c();

	void register_callback(std::function<void(std::wstring, std::wstring, file_watcher_c::e_action)> func);

	dir_watcher_c* register_directory(const std::wstring& path, DWORD notify_filters = e_notify::NOTIFY_ALL);
	bool unregister_directory(const dir_watcher_c* watcher);


	bool start_watching();
	bool stop_watching();

private:
	void run();

	HANDLE get_service_io() const;
	std::function<void(std::wstring, std::wstring, file_watcher_c::e_action)> get_callback() const;

private:
	std::thread m_main_thread;

	volatile bool m_running;
	HANDLE m_service_io_port_handle;

	std::function<void(std::wstring, std::wstring, file_watcher_c::e_action)> m_callback;

	std::vector<watcher_ptr_t> m_watchers;

	management_events_s m_management_events;
};
