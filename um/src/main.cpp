#include <iostream>
#include <Windows.h>
#include <TlHelp32.h>

#include "client.dll.hpp"
#include "offsets.hpp"

static DWORD get_process_id(const wchar_t* process_name) {
	DWORD process_id = 0;

	HANDLE snap_shot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
	if (snap_shot == INVALID_HANDLE_VALUE) 
		return process_id;

	PROCESSENTRY32W entry = {};
	entry.dwSize = sizeof(decltype(entry));

	if (Process32FirstW(snap_shot, &entry) == TRUE)
	{
		if (_wcsicmp(process_name, entry.szExeFile) == 0)
			process_id = entry.th32ProcessID;
		else
		{
			while (Process32NextW(snap_shot, &entry) == TRUE)
			{
				if (_wcsicmp(process_name, entry.szExeFile) == 0)
				{
					process_id = entry.th32ProcessID;
					break;
				}
			}
		}
	}

	CloseHandle(snap_shot);

	return process_id;
}

static std::uintptr_t get_module_base(const DWORD pid, const wchar_t*  module_name) {
	std::uintptr_t module_base = 0;

	HANDLE snap_shot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
	if (snap_shot == INVALID_HANDLE_VALUE)
		return module_base;

	MODULEENTRY32W entry = {};
	entry.dwSize = sizeof(decltype(entry));

	if (Module32FirstW(snap_shot, &entry) == TRUE)
	{
		if (wcsstr(module_name, entry.szModule) != nullptr)
			module_base = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
		else
		{
			while (Module32NextW(snap_shot, &entry) == TRUE)
			{
				if (wcsstr(module_name, entry.szModule) != nullptr)
				{
					module_base = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
					break;
				}
			}
		}
	}

	CloseHandle(snap_shot);

	return module_base;
}

namespace driver {
	namespace codes {
		constexpr ULONG attach = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x696, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);

		constexpr ULONG read = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x697, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);

		constexpr ULONG write = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x698, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	}

	struct Request {
		HANDLE process_id;

		PVOID target;
		PVOID buffer;

		SIZE_T size;
		SIZE_T return_size;
	};

	bool attach_to_process(HANDLE driver_handle, const DWORD pid) {
		Request r;
		r.process_id = reinterpret_cast<HANDLE>(pid);

		return DeviceIoControl(driver_handle, codes::attach, &r, sizeof(r), &r, sizeof(r), nullptr, nullptr);
	}

	template <class T>
	T read_memory(HANDLE driver_handle, const std::uintptr_t addr) {
		T temp = {};

		Request r;
		r.target = reinterpret_cast<PVOID>(addr);
		r.buffer = &temp;
		r.size = sizeof(T);

		DeviceIoControl(driver_handle, codes::read, &r, sizeof(r), &r, sizeof(r), nullptr, nullptr);

		return temp;
	}

	template <class T>
	void write_memory(HANDLE driver_handle, const std::uintptr_t addr, const T& value) {
		Request r;
		r.target = reinterpret_cast<PVOID>(addr);
		r.buffer = (PVOID)&value;
		r.size = sizeof(T);

		DeviceIoControl(driver_handle, codes::write, &r, sizeof(r), &r, sizeof(r), nullptr, nullptr);
	}
}

int main() {
	const DWORD pid = get_process_id(L"cs2.exe");

	if (pid == 0)
	{
		std::cout << "Failed to find process\n";
		std::cin.get();
		return 1;
	}

	const HANDLE driver = CreateFile(L"\\\\.\\KernelDriver", GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

	if (driver == INVALID_HANDLE_VALUE)
	{
		std::cout << "Failed to create driver handle\n";
		std::cin.get();
		return 1;
	}

	if (driver::attach_to_process(driver, pid) == true)
	{
		std::cout << "Attachment successful.\n";

		if (const std::uintptr_t client = get_module_base(pid, L"client.dll"); client != 0) {
			std::cout << "Client found.\n";
			bool glow_toggle = false;
			bool bhop_toggle = false;

			while (true)
			{
				if (GetAsyncKeyState(VK_END))
					break;

				const auto local_player_pawn = driver::read_memory<std::uintptr_t>(driver, client + client_dll::dwLocalPlayerPawn);

				if (local_player_pawn == 0)
					continue;

				const auto flags = driver::read_memory<std::uint32_t>(driver, local_player_pawn + C_BaseEntity::m_fFlags);
			
				// bhop
				const bool in_air = flags & (1 << 0);
				const bool space_pressed = GetAsyncKeyState(VK_SPACE);
				const auto force_jump = driver::read_memory<DWORD>(driver, client + client_dll::dwForceJump);

				if (GetAsyncKeyState(VK_NUMPAD2))
				{
					bhop_toggle = !bhop_toggle;
					Sleep(150);
					printf("Aimbot: %s\n", bhop_toggle ? "on" : "off");
				}

				// glow
				if (GetAsyncKeyState(VK_NUMPAD1))
				{
					glow_toggle = !glow_toggle;
					Sleep(150);
					printf("Glow: %s\n", glow_toggle ? "on" : "off");
				}

				for (auto i = 0; i < 64; i++)
				{
					uintptr_t entity_list = driver::read_memory<uintptr_t>(driver, client + client_dll::dwEntityList);
					if (entity_list == 0)
						continue;
					uintptr_t list_entry = driver::read_memory<uintptr_t>(driver, entity_list + (8 * (i & 0x7FFF) >> 9) + 16);
					if (list_entry == 0)
						continue;
					uintptr_t player = driver::read_memory<uintptr_t>(driver, list_entry + 120 * (i & 0x1FF));
					if (player == 0)
						continue;
					uint32_t player_pawn = driver::read_memory<uint32_t>(driver, player + CCSPlayerController::m_hPlayerPawn);
					uintptr_t list_entry2 = driver::read_memory<uintptr_t>(driver, entity_list + 0x8 * ((player_pawn & 0x7FFF) >> 9) + 16);
					if (list_entry2 == 0)
						continue;
					uintptr_t p_cs_player_pawn = driver::read_memory<uintptr_t>(driver, list_entry2 + 120 * (player_pawn & 0x1FF));
					if (p_cs_player_pawn == 0)
						continue;
					if (glow_toggle)
					{
						driver::write_memory(driver, p_cs_player_pawn + C_CSPlayerPawnBase::m_flDetectedByEnemySensorTime, 100000.f);
					}
					else
					{
						driver::write_memory(driver, p_cs_player_pawn + C_CSPlayerPawnBase::m_flDetectedByEnemySensorTime, 0.f);
					}
					
				}

				// bhop bs
				if (bhop_toggle)
				{
					if (space_pressed && in_air)
					{
						Sleep(5);
						driver::write_memory(driver, client + client_dll::dwForceJump, 65537);
					}
					else if (space_pressed && !in_air)
					{
						driver::write_memory(driver, client + client_dll::dwForceJump, 256);
					}
					else if (!space_pressed && force_jump == 65537)
					{
						driver::write_memory(driver, client + client_dll::dwForceJump, 256);
					}
				}
			}
		}
	}
	
	

	CloseHandle(driver);

	return 0;
}