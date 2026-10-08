#include "memoryAlloc.h"
#include <memory>
#include <shared_mutex>
#include <mutex>
#include <unordered_map>

#include "log/loggerDefine.h"


using namespace IKIGAI;
using namespace IKIGAI::UTILS;
namespace IKIGAI::UTILS {
	std::shared_mutex m_Mutex;
	std::unordered_map<void*, std::size_t> m_Memo;

	bool Record(void* ptr, std::size_t size) {
		std::unique_lock<std::shared_mutex> lock{ m_Mutex };
		auto l_Result = m_Memo.find(ptr);
		if (l_Result != m_Memo.end()) {
			//LOG_WARNING << std::string("Memory: Allocate collision happened");
			return false;
		}
		else {
			m_Memo.emplace(ptr, size);
			return true;
		}
	}

	bool Erase(void* ptr) {
		std::unique_lock<std::shared_mutex> lock{ m_Mutex };
		auto l_Result = m_Memo.find(ptr);
		if (l_Result != m_Memo.end()) {
			m_Memo.erase(ptr);
			return true;
		}
		else {
			//LOG_WARNING << std::string("Memory: MemoryMemo: Deallocate collision happened");
			return false;
		}
	}
}

void* Memory::Allocate(const std::size_t size) {
	auto l_result = ::new char[size];
	Record(l_result, size);
	return l_result;
}

void* Memory::Reallocate(void* const ptr, const std::size_t size) {
	Erase(ptr);
	Record(ptr, size);
	auto l_result = realloc(ptr, size);
	return l_result;
}

void Memory::Deallocate(void* const ptr) {
	Erase(ptr);
	delete[](char*)ptr;
}